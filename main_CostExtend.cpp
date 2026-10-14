// ============================================================
//  main_CostExtend.cpp  (CostExtend ターゲット)
//
//  正本コスト表 costs/costs_<prefix>.csv（t=0..T0-1 のみ定義）を、
//  スケジュール地平線 T_ext（RR/RV 全8条件の最大値）まで拡張した
//  costs_ext/costs_<prefix>.csv を新規生成する。
//
//  方式（ユーザー決定済み）: t<T0 は元の値をそのまま複製（再フォーマットしない）。
//  t>=T0 は資源ごとに最小二乗で c_t = a + b*t + G*s_{t mod 12} を当てはめ、
//  トレンド b*t を t=T0-1 の水準で凍結し、季節性・ノイズだけを延長する:
//      c_t = a + b*(T0-1) + G*s_{t mod 12} + omega_t,  omega_t ~ N(0, sigma)
//  （トレンドをそのまま延長すると値が負になる資源が実測で存在したため。
//   .miss_memory/041 参照）
//
//  読み取り専用: costs/costs_*.csv, j30.sm/ j60.sm/ j90.sm/ j120.sm/
//  書き込み先:   costs_ext/ のみ
// ============================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "problems/RCPSP_MIP_Solver.h"
#include "problems/RCPSP_Conditions.h"

using namespace std;
namespace fs = std::filesystem;

// ============================================================
//  決定論的 RNG（feedback_deterministic_rng_no_stdlib_dist の方式を踏襲）
//  std::normal_distribution / uniform_real_distribution は実装依存なので使わない。
// ============================================================
static uint64_t fnv1a64(const string &s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= (uint64_t)c;
        h *= 1099511628211ULL;
    }
    return h;
}

static double nextUniform01Open(std::mt19937_64 &eng) {
    return ((double)(eng() >> 11) + 0.5) * 0x1.0p-53;
}

static double nextGaussian(std::mt19937_64 &eng) {
    static constexpr double PI = 3.14159265358979323846;
    double u1 = nextUniform01Open(eng);
    double u2 = nextUniform01Open(eng);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * u2);
}

// 季節パターン（Gamma 倍する前の形。s_{t mod 12}）
static const double SEASON[12] = {0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1};

// ============================================================
//  最小二乗フィット: c_t = a + b*t + G*s_{t mod 12}  (t=0..T0-1)
// ============================================================
struct FitResult {
    double a = 0.0, b = 0.0, G = 0.0, sigma = 0.0;
};

static FitResult fitTrendSeasonal(const vector<double> &y) {
    const int T0 = (int)y.size();
    double Sxx[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double Sxy[3] = {0, 0, 0};

    for (int t = 0; t < T0; ++t) {
        double xv[3] = {1.0, (double)t, SEASON[t % 12]};
        for (int i = 0; i < 3; ++i) {
            Sxy[i] += xv[i] * y[t];
            for (int j = 0; j < 3; ++j) Sxx[i][j] += xv[i] * xv[j];
        }
    }

    // Gaussian elimination with partial pivoting (3x3)
    double A[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) A[i][j] = Sxx[i][j];
        A[i][3] = Sxy[i];
    }
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        if (piv != col) std::swap(A[col], A[piv]);
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            double f = A[r][col] / A[col][col];
            for (int c = col; c < 4; ++c) A[r][c] -= f * A[col][c];
        }
    }

    FitResult fit;
    fit.a = A[0][3] / A[0][0];
    fit.b = A[1][3] / A[1][1];
    fit.G = A[2][3] / A[2][2];

    double ss = 0.0;
    for (int t = 0; t < T0; ++t) {
        double pred = fit.a + fit.b * t + fit.G * SEASON[t % 12];
        double res = y[t] - pred;
        ss += res * res;
    }
    int dof = std::max(1, T0 - 3);
    fit.sigma = std::sqrt(ss / dof);
    return fit;
}

// ============================================================
//  main
// ============================================================
int main() {
    try {
        const fs::path costsDir = "costs";
        const fs::path outDir   = "costs_ext";
        fs::create_directories(outDir);

        const vector<string> setNames = {"j120", "j90", "j60", "j30"};

        vector<string> csvFiles;
        for (auto &entry : fs::directory_iterator(costsDir)) {
            if (!entry.is_regular_file()) continue;
            string fn = entry.path().filename().string();
            if (fn.rfind("costs_", 0) == 0 && fn.size() > 10 &&
                fn.compare(fn.size() - 4, 4, ".csv") == 0) {
                csvFiles.push_back(fn);
            }
        }
        sort(csvFiles.begin(), csvFiles.end());

        vector<string> skippedPrefixes;
        long long filesPrefixMatch = 0, filesPrefixMismatch = 0;
        long long totalAdjEqual = 0;
        vector<double> allSigma;
        long long resourcesWithNonpositiveLinear = 0;
        long long totalResources = 0;
        long long totalNClampedToFloor = 0;

        ofstream paramsCsv((outDir / "_extension_params.csv").string());
        paramsCsv << "prefix,k,T0,T_ext,a,b,G,sigma,seed,n_clamped_to_floor,"
                     "linear_min_value,linear_n_nonpositive\n";

        for (const auto &fn : csvFiles) {
            const string prefix = fn.substr(6, fn.size() - 6 - 4);  // strip "costs_" / ".csv"

            string setName;
            for (const auto &s : setNames) {
                if (prefix.rfind(s, 0) == 0 && prefix.size() > s.size() &&
                    std::isdigit((unsigned char)prefix[s.size()])) {
                    setName = s;
                    break;
                }
            }
            if (setName.empty()) {
                skippedPrefixes.push_back(prefix);
                continue;
            }

            const string instFile = setName + ".sm/" + prefix + ".sm";
            if (!fs::exists(instFile)) {
                cerr << "[WARN] instance file not found, skipping: " << instFile << "\n";
                skippedPrefixes.push_back(prefix + " (missing .sm)");
                continue;
            }

            // ---- 元のコスト表を読み込む（t<T0 はトークンをそのまま保持） ----
            ifstream fin((costsDir / fn).string());
            int R = 0, T0 = 0;
            if (!(fin >> R >> T0)) {
                cerr << "[ERROR] failed to read header: " << fn << "\n";
                continue;
            }
            vector<vector<string>> rawTokens(R, vector<string>(T0));
            vector<vector<double>> vals(R, vector<double>(T0));
            bool parseOk = true;
            for (int k = 0; k < R && parseOk; ++k) {
                for (int t = 0; t < T0; ++t) {
                    string tok;
                    if (!(fin >> tok)) { parseOk = false; break; }
                    rawTokens[k][t] = tok;
                    vals[k][t] = stod(tok);
                }
            }
            if (!parseOk) {
                cerr << "[ERROR] failed to parse body: " << fn << "\n";
                continue;
            }

            // ---- 手順1(1): 8条件でホライゾンの最大値 T_ext を求める ----
            int T_ext = 0;
            for (const auto &c : RCPSP_ALL_CONDITIONS()) {
                RCPSP_Problem::resetGlobalCostSeries();
                RCPSP_MIP_Solver solver(instFile, c.rr, c.rv);
                T_ext = std::max(T_ext, solver.getHorizon());
            }
            {
                int formulaMax = 0;
                for (const auto &c : RCPSP_ALL_CONDITIONS()) {
                    int Tf;
                    if (c.rr > 0.0 || c.rv) {
                        double factor = c.rv ? 3.5 : (c.rr >= 0.75 ? 2.5 : 1.5);
                        Tf = (int)(T0 * factor) + 20;
                    } else {
                        Tf = T0;
                    }
                    formulaMax = std::max(formulaMax, Tf);
                }
                if (formulaMax != T_ext) {
                    cout << "[WARN] " << prefix << ": T_ext(actual getHorizon max)=" << T_ext
                         << " != formula-predicted max=" << formulaMax << "\n";
                }
            }
            if (T_ext < T0) T_ext = T0;  // 安全網（理論上到達しない）

            // ---- 手順1(2)-(4): 資源ごとにフィット・延長 ----
            vector<vector<string>> outTokens(R, vector<string>(T_ext));
            for (int k = 0; k < R; ++k)
                for (int t = 0; t < T0; ++t) outTokens[k][t] = rawTokens[k][t];

            long long adjEqualThisFile = 0;

            for (int k = 0; k < R; ++k) {
                FitResult fit = fitTrendSeasonal(vals[k]);
                allSigma.push_back(fit.sigma);
                ++totalResources;

                bool sigmaOut = (fit.sigma < 3.0 || fit.sigma > 7.0);
                int kmod4 = k % 4;
                bool expectPos = (kmod4 == 0 || kmod4 == 2);
                bool signOk = expectPos ? (fit.b > 0.0) : (fit.b < 0.0);
                if (sigmaOut || !signOk) {
                    cout << "[FLAG] " << prefix << " k=" << k
                         << " sigma=" << fit.sigma << " b=" << fit.b;
                    if (sigmaOut) cout << " (sigma out of [3,7])";
                    if (!signOk) cout << " (b sign mismatch for k%4=" << kmod4 << ")";
                    cout << "\n";
                }

                // 診断値: トレンドをそのまま延長した場合の最小値・非正スロット数
                double linMin = 1e300;
                int linNonPos = 0;
                for (int t = 0; t < T_ext; ++t) {
                    double clin = fit.a + fit.b * t + fit.G * SEASON[t % 12];
                    linMin = std::min(linMin, clin);
                    if (clin <= 0.0) ++linNonPos;
                }
                if (linNonPos > 0) ++resourcesWithNonpositiveLinear;

                // ---- 手順1(4): 決定論的シード＋延長生成 ----
                const string seedStr = prefix + "#k=" + to_string(k);
                const uint64_t seed = fnv1a64(seedStr);
                std::mt19937_64 eng(seed);

                const double frozenTrend = fit.a + fit.b * (double)(T0 - 1);
                int nClamped = 0;
                double prevVal = (T0 > 0) ? vals[k][T0 - 1] : 0.0;

                for (int t = T0; t < T_ext; ++t) {
                    double noise = nextGaussian(eng) * fit.sigma;
                    double c = frozenTrend + fit.G * SEASON[t % 12] + noise;
                    if (c <= 0.0) { c = 0.001; ++nClamped; }
                    if (t > T0 && c == prevVal) ++adjEqualThisFile;
                    prevVal = c;

                    ostringstream oss;
                    oss << c;  // 既定のstream書式（元ファイルの writeCostTableToCSV と同一）
                    outTokens[k][t] = oss.str();
                }
                totalNClampedToFloor += nClamped;

                paramsCsv << prefix << "," << k << "," << T0 << "," << T_ext << ","
                          << fit.a << "," << fit.b << "," << fit.G << "," << fit.sigma << ","
                          << seed << "," << nClamped << "," << linMin << "," << linNonPos << "\n";
            }
            totalAdjEqual += adjEqualThisFile;

            // ---- 手順2: t<T0 が文字列として完全一致することを検証 ----
            bool prefixMatch = true;
            for (int k = 0; k < R && prefixMatch; ++k)
                for (int t = 0; t < T0; ++t)
                    if (outTokens[k][t] != rawTokens[k][t]) { prefixMatch = false; break; }
            if (prefixMatch) ++filesPrefixMatch;
            else {
                ++filesPrefixMismatch;
                cerr << "[ERROR] prefix mismatch for " << prefix << " (should never happen)\n";
            }

            // ---- 出力（costs_ext/costs_<prefix>.csv, ヘッダは "R T_ext"） ----
            ofstream fout((outDir / fn).string());
            fout << R << " " << T_ext << "\n";
            for (int k = 0; k < R; ++k) {
                for (int t = 0; t < T_ext; ++t) {
                    fout << outTokens[k][t];
                    if (t + 1 < T_ext) fout << " ";
                }
                fout << "\n";
            }

            cout << "[FILE] " << prefix << "  T0=" << T0 << " -> T_ext=" << T_ext
                 << "  R=" << R << "  -> " << (outDir / fn).string() << "\n";
        }

        // ============================================================
        //  手順2: 集計・検証結果
        // ============================================================
        cout << "\n=== CostExtend verification summary ===\n";
        cout << "files: prefix(t<T0) byte-identical match=" << filesPrefixMatch
             << "  mismatch=" << filesPrefixMismatch << "\n";
        cout << "skipped prefixes (" << skippedPrefixes.size() << "):";
        for (const auto &s : skippedPrefixes) cout << " " << s;
        cout << "\n";
        cout << "adjacent-equal values within extension region t>=T0 (expect ~0): "
             << totalAdjEqual << "\n";
        if (!allSigma.empty()) {
            vector<double> s = allSigma;
            sort(s.begin(), s.end());
            cout << "sigma distribution across all resources (n=" << s.size() << "): "
                 << "min=" << s.front() << "  median=" << s[s.size() / 2]
                 << "  max=" << s.back() << "\n";
        }
        cout << "resources with linear_n_nonpositive>0 (would break under naive trend extension): "
             << resourcesWithNonpositiveLinear << " / " << totalResources << "\n";
        cout << "values clamped to floor 0.001 (n_clamped_to_floor total, expect 0): "
             << totalNClampedToFloor << "\n";
        cout << "params CSV: " << (outDir / "_extension_params.csv").string() << "\n";

        return 0;
    } catch (const exception &e) {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
