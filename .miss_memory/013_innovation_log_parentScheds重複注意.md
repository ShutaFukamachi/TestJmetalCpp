# 教訓: innovation ログ追加時の parentScheds 重複に注意

## 問題
NSGAII.cpp にノベルティフィルタ(NF)用の `parentScheds` (std::set) が既にある状態で、
innovation ログ用にも親スケジュール集合が必要になった。

## 注意点
- NF 用の `parentScheds` は `if (noveltyFilter)` ブランチ内でのみ構築される。
- innovation ログは NF/非NF 両方で動作する必要があるため、別変数 `parentSchedsForInnov` を
  ループ冒頭で常に構築する設計にした。
- 同じ名前の変数を使うとスコープ衝突やNF分岐内での二重構築が起きるため、明確に別名にすること。

## recordChild ラムダの注意
- ラムダ内で `delete` 済みのポインタにアクセスしないよう、`recordChild` は delete 前に呼ぶこと。
- NF 分岐では accepted/rejected の判定後、delete の前に recordChild を呼ぶ設計パターンにした。
- 1e9 フォールバック解（実行不可能解）は `cMs >= 1e8` で検出し、カウンタから除外する。
