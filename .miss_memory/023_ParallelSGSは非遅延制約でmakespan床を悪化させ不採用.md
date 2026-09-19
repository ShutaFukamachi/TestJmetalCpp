# 023: Parallel SGS（本命A）は non-delay 制約で makespan の床を悪化させ、Serial に支配される＝不採用

## 発生日
2026-07-23（PSGS ターゲットでフロント A/B, j309_1 RR050, 本番 eval）

## 結論（先に）
- **Parallel SGS は Serial SGS に（ほぼ全域で）支配される。採用しない。**
- 決定打: **makespan の床が悪化**。Serial+A1=**139**（現ベースライン）に対し、
  **Parallel=159 / Parallel+A1=159**。Parallel は 139 に到達できない。

## グラフの読み（psgs_front_j309_1_RR050.png）
1. ms* floor: Serial=159, Parallel=159, **Serial+A1=139**, **Parallel+A1=159**。
   → 139 に届くのは Serial+A1 のみ。Parallel は A1 併用でも 159 止まり。
2. 中盤(ms 165–200): 赤(Parallel)線が青(Serial)線の**右上**＝同じ makespan でコスト高
   or 同じコストで makespan 大＝Parallel が劣位。
3. コスト端(ms≈250): Serial ≈413k < Parallel ≈415k。コスト側も Serial が良い。

## 根本原因（重要な理論的教訓）
- **Parallel SGS が生成するのは non-delay schedule。これは active schedule の真部分集合。**
  RCPSP では **makespan 最適スケジュールは active だが non-delay とは限らない**
  （資源が空いていても敢えてジョブを遅らせる＝意図的アイドルが必要な場合がある）。
- j309_1 RR050 の良い makespan 解（139、真の最適125）は **active-but-NOT-non-delay**。
  よって non-delay に限定する Parallel は**その領域を構造的に表現できない**。
- 実証: **Serial で 139 を出す A1 優先規則リストを、そのまま Parallel でデコードすると 159**。
  同じ優先度リストでも非遅延貪欲だと良い makespan 解を捨てる。021 の仮説
  「Parallel が早い資源を空けてクリティカルを前倒し」は **この問題では成立せず**、逆に悪化した。

## 021 の「本命」判断が外れた理由
- 021 は「makespan 床は生成スキーム問題」と正しく診断したが、処方を誤った。
  Serial(active) は既に **より広い到達集合**を持ち最適を含む。問題は「scheme が狭い」のでなく
  「Serial の active 集合の中で 125 に至る順列を GA が探索できていない」（探索圧不足, 事実②）。
  Parallel は集合をさらに**狭める**方向で、逆効果だった。

## 次の含意（ユーザー提示待ち）
- 良い解は「意図的な遅延（アイドル）」を要する active-not-non-delay 領域にある。
  → 生成スキームを**狭める**のでなく、**遅延を明示的に表現/探索できる構造**が筋:
    - **C10 双方向シフト**（負shift=右詰めで非クリティカルを遅らせる）＝意図的アイドルを
      エンコーディングで導入。Parallel と逆に到達集合を広げる方向。
    - もしくは D群（探索圧強化）で Serial の active 集合内の 125 順列を掘る。
- Parallel コードは default OFF で残置（`setParallelSGS`）。負の結果として記録。
- 手法可否は best でなく**フロント全体（支配関係）と床**で判定（feedback_drop 準拠）。
- 関連: [[021]]（生成スキーム診断）, [[022]]（serial phantom, 検証で発覚）, [[019]]（真の最適125）。
