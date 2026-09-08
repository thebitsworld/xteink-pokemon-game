---
title: Pokémon Battle Roadmap
parent: Development
nav_order: 5
---

# Pokémon Battle System — Roadmap

Tài liệu bàn giao cho việc mở rộng module Pokémon thành một "Pokémon Red thu nhỏ": học chiêu theo level, vật phẩm/TM/HM, chiến đấu turn-based, bắt Pokémon bằng bóng, 8 gym + Elite Four, huy hiệu.

Nhánh làm việc: **`feat/pokemon-battle-system`**. Đọc [pokemon-mechanics.md](./pokemon-mechanics.md) trước để hiểu cơ chế game hiện tại.

Ai tiếp nhận công việc này: đọc mục **Ràng buộc bất di bất dịch** trước tiên — đó là những thứ đã được khảo sát code kỹ và vi phạm sẽ gây hỏng save của người dùng hoặc vỡ build.

---

## Ràng buộc bất di bất dịch

Năm điều dưới đây rút ra từ việc đọc code thật, không phải suy đoán. Vi phạm bất kỳ điều nào đều gây hậu quả nghiêm trọng và khó phát hiện.

**1. Ngân sách flash chỉ còn ~230KB.**
`pokemon-x3` hiện chiếm **96.2%** OTA partition (6,303,483 / 6,553,600 byte, còn **235,968 byte**). `scripts/check_firmware_size.py` fail build khi vượt. Toàn bộ thiết kế phải bám ngân sách này; build đo lại sau mỗi giai đoạn.

**2. `PokemonState` chỉ được mọc thêm SAU byte 115.**
`src/pokemon/PokemonStore.cpp:265` có `write32(stateBytes.data() + 108, nextSequence)` — offset viết cứng, trùng lặp với codec. Nếu layout mới đẩy `sequence` khỏi offset 108 thì dòng này ghi đè lên field khác *sau khi* `encodeState()` đã validate → hỏng save âm thầm. Giữ nguyên toàn bộ layout 0..115, field mới bắt đầu từ 116.

**3. KHÔNG nới `PokemonRecord` (48 byte).**
`RecordBytes` là `std::array<uint8_t,48>` — kích thước nướng vào *kiểu*, dùng trong 6 vòng lặp stream; `decodeSnapshotHeader()` hardcode "record phải đúng 48 byte" ở 2 chỗ (`PokemonStoreCodec.cpp:100,102`); vòng copy-forward khi commit chép record dạng byte thô nên không tự nới được; `decodeRecord()` còn kiểm `bytes[47] != 0` bằng số viết cứng.
→ Dữ liệu chiến đấu (4 chiêu, PP, HP, status) nằm ở **file phụ** `/.crosspoint/pokemon-battle.bin`, không nhét vào record.

**4. Tên chiêu/vật phẩm KHÔNG đi qua i18n.**
Mỗi key i18n tốn `28 ngôn ngữ × 2 byte` bảng offset dù chỉ có tiếng Anh → ~250 key sẽ tốn ~14KB offset + ~4KB text. Tệ hơn: `scripts/gen_i18n.py` chạy `strip_unused=True` và **chỉ quét `src/` + `lib/`**, không quét header sinh trong `$BUILD_DIR` → key chỉ được tham chiếu từ bảng dữ liệu sinh tự động sẽ bị **xóa khỏi firmware**.
→ Theo tiền lệ sẵn có: tên 151 loài là chuỗi C thô trong header sinh tự động. Tên chiêu/vật phẩm làm y hệt. Chỉ ~20 chuỗi UI mới dùng i18n.
→ Blob tiếng Anh có trần cứng **32,767 byte** (offset 15 bit, `gen_i18n.py` raise `ValueError` khi vượt).

**5. KHÔNG sửa `scripts/data/pokemon-kanto-v2.csv`.**
`test/pokemon_types/PokemonSpeciesGeneratorTest.py` ghim SHA-256 của nội dung file này (`EXPECTED_METADATA_SHA256`). Cần thêm dữ liệu thì tạo CSV mới, không đụng file cũ.
Ngoài ra test đó assert `CPPPATH` sau khi chạy generator có **đúng 1 phần tử** → mọi generator mới phải xuất ra **cùng** thư mục `$BUILD_DIR/generated/pokemon`.

---

## Quyết định thiết kế đã chốt

Ghi lại kèm lý do, để người tiếp nhận không phải tranh luận lại:

| Quyết định | Lý do |
|---|---|
| HP/PP/status **giữ nguyên giữa các trận** | Người dùng chọn, để giống Red thật |
| Status effect làm **đầy đủ ngay từ đầu** (ngủ/tê/độc/bỏng/băng/rối loạn) | Người dùng chọn; đồ chữa status mới có ý nghĩa |
| **Đọc sách hồi HP/PP dần**, đầy máu thì xóa status | Chống ngõ cụt: hết Potion + cả Party kiệt sức = kẹt vĩnh viễn. Cũng đúng tinh thần thiết bị đọc sách |
| Dữ liệu chiến đấu ở **file phụ**, không nới record | Xem ràng buộc 3. File phụ **tái tạo được 100%** (chiêu suy từ learnset theo level, HP/PP đầy) → hỏng file = dựng lại, không bao giờ mất Pokémon |
| Dùng **base stat thật** của 151 loài | Không có stat riêng thì mọi Pokémon cùng level đánh y hệt nhau, type matchup thành yếu tố duy nhất. Chỉ tốn ~755 byte |
| Gym team rút còn **2-3 con** (bản gốc tới 5) | Mỗi lượt đánh = 1 lần refresh e-ink toàn màn (~1 lần lật trang sách). Đội 5 con làm trận đấu lê thê |
| Huy hiệu **chỉ là thành tựu trưng bày** | Người dùng chọn; giữ rủi ro cân bằng thấp nhất, không phải đụng `PokemonGame.cpp` |
| Gym mở khóa **tuyến tính**, đủ 8 huy hiệu mới mở Elite Four | Tạo đường tiến triển — thứ game hiện đang thiếu |
| Thua **không bị phạt** | Rào cản tự nhiên là level Pokémon, mà level chỉ lên bằng đọc sách thật |

---

## Trạng thái hiện tại

### GĐ 0 — Dữ liệu nguồn ✅ XONG

`scripts/fetch_pokemon_battle_data.py` lấy dữ liệu Gen 1 (Red/Blue) thật từ PokeAPI, cache response xuống `scripts/.pokeapi-cache/` (đã gitignore) nên chạy lại rất nhanh.

| File | Số dòng | Cột |
|---|---|---|
| `scripts/data/pokemon-stats.csv` | 151 | `id,name,hp,attack,defense,special,speed` |
| `scripts/data/pokemon-moves.csv` | 165 | `id,name,type,power,accuracy,pp,damage_class,ailment,ailment_chance` |
| `scripts/data/pokemon-learnsets.csv` | 989 | `species_id,level,move_id` |
| `scripts/data/pokemon-tmhm.csv` | 3037 | `species_id,move_id` |
| `scripts/data/pokemon-gyms.csv` | 12 | `order,leader,badge,type,team` (viết tay — PokeAPI không có dữ liệu gym) |

Lưu ý: PokeAPI chặn User-Agent mặc định của `urllib` (403) — script đã set header riêng.
Gen 1 chỉ có một chỉ số "Special"; script dùng `special-attack` của PokeAPI làm giá trị tương ứng.

---

## Các giai đoạn còn lại

Mỗi giai đoạn là một điểm dừng tự nhiên: build được, test được, commit được.

### GĐ 1 — Bảng vật phẩm + 5 generator C++ ✅ XONG (commit `eff18d13`)

- [x] `scripts/data/pokemon-items.csv`: 83 vật phẩm. Id `1..6` trùng `EvolutionItem` hiện có (đá tiến hóa + Link Cable, được `generate_pokemon_items.py` tự kiểm tra bằng `PINNED_STONE_NAMES`); `7..10` bóng; `11..17` hồi máu; `18..23` chữa status; `24..28` Rare Candy + hồi PP; `29..78` TM01-50; `79..83` HM01-05. Cột thật dùng: `id,name,category,effect_value,cures_ailment,teaches_move_id,drop_weight`.
- [x] Ánh xạ TM/HM → move id: **không** gọi thêm PokeAPI — đối chiếu 55 tên chiêu Gen 1 chuẩn với `pokemon-moves.csv` đã có sẵn để lấy đúng id (script đối chiếu một lần, không cần lưu lại).
- [x] 5 cặp generator theo đúng khuôn `generate_pokemon_v2_species{,_build}.py`. `generate_pokemon_learnsets.py` gộp cả learnset **và** TM/HM compat từ 2 CSV vào 1 header (`PokemonLearnsets.generated.h`) như roadmap dự tính.
- [x] `MoveListRef{uint16_t offset; uint8_t count;}` (khác tên `LearnsetOffset` dự tính ban đầu) dùng chung cho cả learnset lẫn TM/HM offset/count.
- [x] Đăng ký `pre:` script trong cả `[env:pokemon-x3]` và `[env:pokemon-simulator-X3]`.
- [x] `test/pokemon_battle_data/`: `PokemonBattleDataTest.cpp` (native, đối chiếu số liệu thật: Chansey HP=250, Bulbasaur learnset 9 entries bắt đầu Tackle/Growl ở level 1, tổng learnset=989, tổng TM/HM=3037, id 1-6/29-83 khớp EvolutionItem/Machine...) + `PokemonBattleDataGeneratorsTest.py` (kiểu `PokemonSpeciesGeneratorTest.py`, kiểm CLI + CPPPATH + đăng ký platformio.ini). Cả 2 đã thêm vào `test/CMakeLists.txt`.

**Kết quả đo được** (không phải ước lượng): `pio run -e pokemon-x3` build sạch, Flash tăng đúng **+104 byte** so với baseline 6,303,483 B — vì chưa có code nào gọi `moveData()`/`baseStatsFor()`/... nên linker loại bỏ hết bảng dữ liệu chưa dùng. Tức là **toàn bộ lớp dữ liệu GĐ1 gần như miễn phí** cho tới khi GĐ2+ thực sự dùng tới. 16/16 test Pokémon native pass (`ctest -R Pokemon` trong `test/build`).

**Sai lệch nhỏ so với dự tính ban đầu** (đáng lưu ý cho người tiếp nhận):
- `Tackle` (move id 33) power=**40** không phải 35 — PokeAPI trả giá trị hiện tại (đã buff từ Gen 6), không phải giá trị gốc Gen 1. Chấp nhận được vì hầu hết move khác không đổi qua các gen; chỉ vài move ngoại lệ như Tackle.
- `Tri Attack` có `ailment=None` dù `ailment_chance=20` — vì nó gây 1-trong-3 trạng thái ngẫu nhiên, PokeAPI trả `ailment="unknown"`, model đơn-ailment của mình không nắm bắt được. Chấp nhận là giới hạn đã biết của bản rút gọn Gen 1.

### GĐ 2 — Engine chiến đấu thuần ✅ XONG (commit `f1ff1bd2`)

- [x] Tra cứu chiêu (`moveData`), learnset/TM-HM (`learnsetFor`/`canLearnViaMachine`) đã có sẵn từ GĐ1 (`PokemonMoveData.cpp`, `PokemonLearnsets.cpp`) — không cần làm lại.
- [x] `lib/Pokemon/PokemonTypeChart.cpp`: bảng khắc chế 18×18 **viết tay** (không qua CSV/generator vì không đổi độc lập với code đọc nó) — dùng bảng hiện đại (post-Gen 6, đủ Dark/Steel/Fairy) thay vì bảng gốc Gen1 thiếu 3 type này, nhất quán với việc dữ liệu move/stat đã lấy giá trị PokeAPI hiện tại.
- [x] `lib/Pokemon/PokemonBattle.h/.cpp`: `BattleCombatant` (speciesId/level/HP/moves/status — không lưu type/base stat, tra lại qua `speciesData()`/`baseStatsFor()` mỗi lần cần, giống cách `PendingEvent` chỉ lưu speciesId). Damage theo công thức Gen1 (không có IV/EV vì không model), STAB, khắc chế, random 85-100%. Đủ 6 status effect. Turn order theo Speed thật (paralysis giảm nửa tốc). KO kết thúc lượt ngay, không cho bên thua phản đòn. `attemptCatch()` dùng `captureRate` có sẵn.
- [x] Engine chỉ trả enum `BattleLogEvent`, không có chuỗi text nào — UI (GĐ5+) tự dịch qua `tr(STR_*)`.
- [x] `RandomSource` tái dùng nguyên struct từ `PokemonGame.h` (public); `randomBelow`/`rollBelow` viết lại bản riêng trong `PokemonBattle.cpp` vì helper gốc trong `PokemonGame.cpp` nằm trong anonymous namespace, không export được.
- [x] `test/pokemon_battle/PokemonBattleTest.cpp`: 10 test native (damage, khắc chế 2 chiều, status, paralysis+giảm speed, poison/burn tick, KO ngay lập tức, catch rate 2 loại bóng × 2 mức HP/status). 17/17 test Pokémon pass.

**Lỗi dữ liệu phát hiện khi nối dây (không phải do test fail — do soát tay CSV):** `ailment_chance` từ PokeAPI = 0 cho **hiệu ứng chính** của chiêu Status thuần (Toxic, Sleep Powder, Confuse Ray...), nghĩa là "chắc chắn 100%" theo quy ước của PokeAPI — không phải "0% không bao giờ". Ban đầu code coi 0 = không bao giờ áp trạng thái, khiến cả nhóm chiêu Status gây hiệu ứng **không hoạt động**. Đã sửa: chiêu Status với `ailmentChance==0` → coi là 100%; chiêu sát thương có hiệu ứng phụ (Ember 10% bỏng...) giữ nguyên số liệu thật. **Bài học cho các GĐ sau**: cần soát tay thêm các field dạng %/hiếm tương tự trước khi tin tưởng dùng thẳng.

**Giới hạn phạm vi đã biết** (không phải thiếu sót, là ranh giới scope): không model stat-stage (Attack/Defense/Speed/Accuracy/Evasion boost từ Growl, Swords Dance, Reflect...) — các chiêu này chỉ có thể trúng/trượt, không có hiệu ứng gì thêm. Không model crit hit. Move target luôn là đối thủ (không có self-target như Rest).

### GĐ 3 — Lưu trữ ✅ XONG (commit `da111328`)

- [x] `PokemonState` v3: append `bagCounts[77]` (uint8) và `uint16_t battleProgress` (8 bit gym + 4 bit Elite Four, 4 bit dự phòng) **sau byte 115**. `itemCounts[6]` cũ giữ nguyên offset 54..65.
- [x] `snapshotStateBytes()` + `decodeState()` thêm nhánh v2 (`POKEMON_SNAPSHOT_VERSION_V2 = 2`, field mới = 0 khi decode). Migration chạy tự động vì commit luôn ghi version hiện tại (v3).
- [x] `validateState()` ràng buộc `battleProgress` (bit dự phòng phải =0) — thỏa mãn với state v2 zero-extend, save cũ vẫn đọc được bình thường.
- [x] `PendingEventKind::MoveLearn` (=4): dùng lại đúng 10 byte sẵn có (`recordId` = Pokémon, trường `speciesId` chứa moveId, `level` = level học) → chỉ thêm case vào `validatePendingEvent()`, không đổi kích thước `PendingEvent`.
- [x] `lib/Pokemon/PokemonBattleStoreCodec.h/.cpp` (mã hóa thuần, dùng `std::array` cố định — không `std::vector`, đúng convention toàn module) + `src/pokemon/PokemonBattleStore.h/.cpp` (I/O qua `HalStorage`, cùng idiom với `PokemonStore.cpp`): file phụ `/.crosspoint/pokemon-battle.bin`, 16 byte/entry (`recordId(4) + moves[4] + pp[4] + currentHp(2) + status(1) + statusTurns(1)`) + CRC32 toàn file, tối đa `PARTY_SIZE=6` entry (PC không cần track). Tạo lazy qua `load()` tự gọi ở lần dùng đầu (kể cả từ hàm `const` nhờ `mutable`); CRC sai hoặc thiếu entry → coi như rỗng, không bao giờ chặn người chơi.
- [x] Test: `test/pokemon_battle_store/` (`PokemonBattleStoreCodecTest` — mã hóa thuần; `PokemonBattleStoreTest` — I/O qua stub `HalStorage` có sẵn ở `test/pokemon_store/stubs/`) + thêm case vào `PokemonStoreCodecTest.cpp` cho v2→v3 zero-extend và battleProgress reserved-bit.
- [x] Cập nhật `docs/file-formats.md` với layout v3 đầy đủ và format `pokemon-battle.bin`.

**Lỗi phát hiện khi sửa test cũ (không phải bug logic)**: 3 test trong `PokemonStoreTest.cpp`/`PokemonStoreCodecTest.cpp` dùng số "3" làm giá trị version giả lập "chưa hỗ trợ" — giờ 3 chính là `POKEMON_SNAPSHOT_VERSION` thật, phải đổi sang `POKEMON_SNAPSHOT_VERSION + 1`. Bài học: nên dùng giá trị tương đối (`CURRENT+1`) thay vì số cứng khi ý định là "giá trị không hợp lệ", để tránh vỡ khi version tăng.

### GĐ 4 — Service + rơi đồ ✅ XONG (commit `18a0eeca`) — mốc đo dung lượng đầu tiên có ý nghĩa

- [x] `PokemonService` thêm dependency `PokemonBattleStore&` (constructor giờ nhận 3 tham số: `store, battleStore, random` — đã cập nhật cả `devicePokemonService()` lẫn 19 call site trong `PokemonServiceTest.cpp` bằng sed).
- [x] `consumeBagItem(itemId)` — trừ 1 ở `itemCounts` (đá, id 1-6) hoặc `bagCounts` (còn lại, id 7-83).
- [x] `markGymDefeated(gymIndex)` — set bit `battleProgress`, ép mở khóa tuyến tính (gym N cần 1..N-1 đã hạ; Elite Four cần đủ 8 gym). Idempotent nếu gọi lại gym đã hạ. Không đụng XP/encounter/`PokemonGame.cpp`.
- [x] `loadBattleEntry(recordId)` — trả entry có sẵn trong battle store, hoặc **tự tổng hợp** từ level + learnset (duyệt ngược learnset để lấy 4 chiêu học gần nhất ở level hiện tại) tại HP/PP đầy, không status — rồi lưu lại để lần sau khỏi tổng hợp lại. `saveBattleEntry()` ghi ngược lại.
- [x] Hồi phục khi đọc: `creditMinutes()` sau khi commit thành công sẽ hồi HP/PP cho **battle entry đã tồn tại** của từng thành viên Party (+1 HP/phút đọc, +1 PP/chiêu mỗi 10 phút, đều có trần; hết máu status tự xóa). Pokémon chưa có entry thì bỏ qua (sẽ tự tổng hợp đầy đủ máu khi cần) — không đụng `PokemonGame.cpp`.
- [x] Mở rộng `createItem()` — đúng như dự tính, **thay đổi duy nhất** chạm `PokemonGame.cpp`: chọn theo trọng số (`ItemData::dropWeight`) trên toàn bộ 83 vật phẩm, vẫn ưu tiên đá tiến hóa Pokémon đang sở hữu cần trước (giữ nguyên hành vi cũ khi có nhu cầu thật). Giữ lại tối ưu "bỏ qua roll khi chỉ có 1 ứng viên" của code cũ — vừa đỡ tốn 1 lần random, vừa giữ tương thích với test đã script sẵn chuỗi random.
- [x] `PendingEventKind::Item`'s `item` field giờ mang ý nghĩa id vật phẩm chung 1-83 (không chỉ `EvolutionItem` 1-6) — nới `validatePendingEvent`, giữ id 1-6 khớp `EvolutionItem` nên save cũ vẫn đọc đúng.
- [x] Test: 6 test mới trong `PokemonServiceTest.cpp` (đối chiếu số liệu thật: Pikachu tổng hợp đúng 2 chiêu [Thunder Shock, Growl] ở level 5, đúng bit gym, đúng catch math). 2 test trong `PokemonGameTest.cpp` phải sửa vì thuật toán chọn item đổi thật (không phải do bug — "6 đá đầy = không rơi gì" không còn đúng nữa vì còn 77 vật phẩm khác).
- [x] **`pio run -e pokemon-x3`**: Flash **6,314,621 B (96.4%, còn 224,832 B ≈ 220KB)** — tăng thật **+10,588 B** so với GĐ3 (6,304,033 B), vì giờ `PokemonService` **thực sự gọi** `moveData()`/`baseStatsFor()`/`learnsetFor()`/`itemData()` nên linker không loại bỏ được nữa. Đây đúng là điểm ngoặt roadmap dự đoán — GĐ1/2 gần như miễn phí, GĐ3/4 mới là chi phí thật.

**Chưa chạy được simulator để test runtime thật** — máy build thiếu `libsdl2-dev`, không cài được do `sudo` cần xác thực tương tác (không có trong môi trường phi tương tác). Firmware `pokemon-x3` build sạch + 19/19 test pass là bằng chứng duy nhất có được ở giai đoạn này; **chưa có UI nên chưa có gì khác biệt để thấy khi chạy trên thiết bị thật** — GĐ4 chỉ là lớp nền, cần GĐ5+ mới "chơi được".

### GĐ 5 — UI: Battle + bắt bằng bóng ✅ XONG (commit `a989d161`)

- [x] Thêm `Screen::Battle`, `BattleMoves`, `BattleBalls` — **3 màn, không phải 4 như dự kiến ban đầu**: bỏ `BattleBag` vì engine chưa (và không có kế hoạch) hỗ trợ dùng item hồi phục giữa trận — chỉ có FIGHT (moves) và BALL (catch) là hành động thật.
- [x] Nối vào nhánh Encounter của `Screen::Event`: "Catch" mở `enterBattle()` thay vì bắt ngay; ném bóng thành công tái sử dụng nguyên `resolveEncounter(Catch, ...)` + luồng đặt nickname sẵn có; thất bại → "broke free", quay lại Battle. "Pass"/RUN đều gọi `resolveBattleAsPass()`.
- [x] Vẽ tự do trong `renderBattleHud()` (gọi từ `renderFocused()`): tên+level+HP bar (`fillRect`/`drawRect`) hai bên, status abbreviation, `drawPokemonSpeciesArt`, và `battleLog_` (build bởi `buildBattleLog()` từ `BattleTurnResult`).
- [x] `PokemonService` thêm `resolveBattleTurn()`/`attemptBattleCatch()` bọc `pokemon::stepBattle()`/`attemptCatch()` bằng `random_` riêng — UI không bao giờ chạm `RandomSource` trực tiếp, giữ đúng nguyên tắc kiến trúc cũ.
- [x] `PokemonBattle` tách `defaultMovesetForLevel()` dùng chung giữa `PokemonService::synthesizeBattleEntry()` (đã có từ GĐ4) và `PokemonActivity::enterBattle()` (mới) — tránh lặp lại logic quét learnset.
- [x] **Sửa 1 bug tồn tại từ GĐ4** (phát hiện khi đọc lại `itemName()`): hàm chỉ xử lý 6 `EvolutionItem`, item id 7-83 (mở rộng bởi `PendingEventKind::Item` ở GĐ4) hiện tên rỗng. Fallback sang `pokemon::itemData(...)->name`.
- [x] 17 key i18n mới cho log trận đấu (không phải ~20 như ước lượng ban đầu — không cần `STR_POKEMON_NO_PP`, message đó dùng `showMessage` với chuỗi có sẵn khác).
- [x] Test: 19/19 suite native pass; `PokemonServiceTest` 27/27 (thêm 2 test cho 2 wrapper method mới); `test/pokemon_battle/CMakeLists.txt` cần bổ sung `PokemonLearnsets.generated.h` + `PokemonLearnsets.cpp` (link error vì `defaultMovesetForLevel` mới kéo theo dependency này).
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,326,799 B (96.5%, còn 212,656 B ≈ 208KB)** — tăng **+12,178 B** so với GĐ4 (6,314,621 B), cho toàn bộ UI Battle + 17 key i18n.

**Phát hiện quan trọng (rủi ro cho mọi giai đoạn sau còn sửa i18n)**: sau khi xóa 1 key giữa danh sách trong `english.yaml` (dịch chuyển toàn bộ `StrId` enum phía sau), chạy lại `pio run` báo "up to date" trong 3.2s mà **không rebuild `I18nStrings.o`** — SCons không phát hiện thay đổi qua build hook generator (`gen_i18n.py` chạy như `pre:` script, không nằm trong dependency graph mà SCons theo dõi). Xác nhận qua `stat -c "%Y %n"`: object cũ hơn source mới generate ra. Nguy cơ: link nhầm object cũ với enum layout mới → tra sai chuỗi ở runtime, **sai lặng lẽ, không có lỗi build nào báo**. **Bắt buộc**: sau mỗi lần sửa file trong `lib/I18n/translations/*.yaml`, chạy `rm -rf .pio/build/<env>` để force rebuild sạch trước khi tin tưởng số liệu flash hay hành vi runtime.

### GĐ 6 — UI: Gym List + Badges ✅ XONG (commit `2f420118`) — gym battle thật, không chỉ hiển thị

- [x] Thêm `Screen::GymList` (12 dòng: 8 gym + 4 Elite Four), `Screen::Badges` (8 dòng huy hiệu gym) + 2 mục vào `Screen::Menu` ("GYM BATTLE", "BADGES", đẩy toggle home-screen và Reset xuống index 7/8).
- [x] Hiển thị trạng thái từng dòng: Defeated / Locked / trống (có thể thách đấu ngay); Elite Four khóa tới khi đủ 8 huy hiệu — logic mở khóa tuyến tính đọc qua `pokemon::gymProgressFor(battleProgress, gymIndex)` (mới, `PokemonBattleTypes.h`/`PokemonGymData.cpp`), dùng chung với `PokemonService::markGymDefeated()` để luật chỉ tồn tại một chỗ (`markGymDefeated` được refactor lại để gọi hàm này thay vì tính lại bit mask).
- [x] **Vượt phạm vi checklist gốc (chỉ ghi "hiển thị trạng thái") vì nếu không thì màn hình vô dụng**: chọn 1 gym đang mở sẽ thực sự vào trận — tái sử dụng nguyên `Screen::Battle`/`BattleMoves`/`BattleBalls` của GĐ5, không tạo màn hình chiến đấu riêng. Khác biệt so với gặp hoang dã, theo dõi bằng `gymChallengeIndex_`/`gymChallengeTeamProgress_`:
  - Không có nút BALL (`logicalCount()` trả 2 thay vì 3 khi đang đấu gym) — không bắt được Pokémon của trainer.
  - Đối thủ gục không kết thúc trận: `advanceGymOpponentOrFinish()` tung tiếp thành viên kế trong đội 2-3 con; hạ hết đội mới gọi `service_.markGymDefeated()` + hiện thông báo huy hiệu (`STR_POKEMON_BADGE_EARNED` có huy hiệu / `STR_POKEMON_TRAINER_DEFEATED` cho Elite Four không huy hiệu).
  - Thua (`finishGymChallenge(false)`) hoặc bỏ chạy/Back (`resolveBattleAsPass()` rẽ nhánh khi `gymChallengeIndex_ != 0`) đều không đụng `battleProgress` — không giống gặp hoang dã, gym battle không có `PendingEvent` nên không cần "Pass" qua service, chỉ đơn giản rời trận.
- [x] Tách `setupBattlePlayer()`/`setupBattleOpponent()` từ `enterBattle()` cũ (GĐ5) để `enterGymBattle()` dùng lại nguyên vẹn thay vì chép code khởi tạo `BattleCombatant`.
- [x] 8 key i18n mới: `STR_POKEMON_GYM_BATTLE`/`BADGES` (menu+tiêu đề), `STR_POKEMON_GYM_LOCKED`/`DEFEATED` (trạng thái dòng), `STR_POKEMON_ELITE_FOUR` (tiền tố nhãn dòng E4), `STR_POKEMON_BADGE_EARNED`/`TRAINER_DEFEATED` (thắng), `STR_POKEMON_GYM_CHALLENGE_LOST` (thua).
- [x] `test/pokemon_service/CMakeLists.txt` cần bổ sung generator `pokemon-gyms.csv` + `PokemonGymData.cpp` (link error tương tự GĐ5's learnsets fix — `PokemonServiceTest` giờ kéo theo `gymProgressFor`/`gymData` qua `markGymDefeated`).
- [x] Test: 19/19 suite native pass (không có test UI riêng cho `PokemonActivity.cpp` — file này không nằm trong test suite native, chỉ verify qua build thật + smoke chạy simulator ngắn không crash khi khởi động).
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,329,615 B (96.6%, còn 209,840 B ≈ 205KB)** — tăng **+2,816 B** so với GĐ5 (6,326,799 B), rất nhỏ vì tái dùng dữ liệu gym đã sinh sẵn từ GĐ1 (trước đó chưa ai gọi tới, bị linker loại bỏ) và hạ tầng list/i18n có sẵn.

### GĐ 7 — UI: 4 chiêu ở Summary + học/thay chiêu ✅ XONG (commit `5f496ad6`)

- [x] Khối 4 chiêu trong `renderFocused()` nhánh Summary — **2 chiêu/dòng** (không phải 1/dòng) để chỉ tốn 2 dòng thay vì 4, đúng tinh thần "rút gọn khi landscape" đã lo trước; đọc qua `service_.peekBattleMoves()` (mới) — **không bao giờ** tạo/ghi entry vào battle store (khác `loadBattleEntry()`), vì Summary phải giữ đúng nghĩa "chỉ đọc": mở màn hình xem thông tin không được phép âm thầm ghi SD.
- [x] Giữ Summary **chỉ đọc** (`logicalCount()` vẫn trả 0) — không đổi.
- [x] `PendingEventKind::MoveLearn` (khai báo từ GĐ3, chưa ai tạo ra event này) giờ được sinh thật: `PokemonService::queueMoveLearnIfNeeded()` (gọi trong `creditMinutes()` trước khi commit) duyệt `learnsetFor(leader)` trong khoảng `(previousLevel, currentLevel]` — còn ô trống thì tự học thẳng vào battle store (không cần hỏi), đủ 4 ô thì `enqueuePendingEvent(MoveLearn)` cho UI hỏi. Pokémon chưa từng chiến đấu (chưa có battle entry) bị bỏ qua có chủ đích — trận đầu tiên của nó đã tự tổng hợp đúng 4 chiêu mới nhất theo cấp hiện tại (cơ chế có sẵn từ GĐ4), không có gì để "bắt kịp".
- [x] `pokemon::acknowledgeMoveLearn()` (mới, `PokemonGame.cpp`, thuần — giống hệt `acknowledgeItem`) dequeue event sau khi UI xử lý; `PokemonService::resolveMoveLearn(replaceSlot)` — slot 0-3 học đè ô đó, slot khác (vd -1) bỏ qua — cả hai đều dequeue.
- [x] UI: `Screen::Event` thêm nhánh MoveLearn — 5 dòng (4 chiêu hiện tại + Cancel), tái dùng nguyên `activate()`/`buildRows()`/`logicalCount()` cơ chế list sẵn có cho Encounter/Evolution, không cần screen mới.
- [x] Dạy chiêu bằng TM/HM: mở rộng `Screen::Bag` từ chỉ 6 đá tiến hóa sang gồm cả 55 vật phẩm Machine (id không liền dải trong data — duyệt bằng `machineItemIdAt()`/`machineItemCount()` thay vì hard-code offset). `PokemonService::teachMove(recordId, moveId)`: còn ô trống thì học luôn, đã biết thì `AlreadyKnown`, đủ 4 ô thì `MovesetFull`.
- [x] **Giản lược có chủ đích so với ý định gốc**: dùng TM khi moveset đầy **không có** màn hình chọn-ô-để-thay (khác hẳn với MoveLearn tự động ở trên, cái đó CÓ 5-dòng picker) — chỉ báo "đã biết đủ 4 chiêu, học bớt 1 chiêu trước đã" và không tiêu TM. Xây thêm 1 luồng chọn-ô-thay riêng cho TM (không đi qua `PendingEvent` queue, cần state/screen riêng) là chi phí không tương xứng ở giai đoạn này; trường hợp phổ biến (Pokémon mới bắt/mới lên cấp, còn ô trống) vẫn hoạt động đầy đủ — ai muốn dạy TM khi đã đủ 4 chiêu thì tự vào Summary xem trước, biết chiêu nào muốn giữ, rồi lên cấp lần sau để trigger đúng luồng MoveLearn có picker.
- [x] Bag icon: `renderRowArt()` giới hạn chỉ vẽ icon cho 6 đá tiến hóa gốc (không có asset icon cho 55 vật phẩm Machine).
- [x] Test: 8 test mới `PokemonServiceTest.cpp` (auto-học vào ô trống, queue MoveLearn khi đầy, `resolveMoveLearn` học/bỏ qua, `teachMove` 3 outcome, `peekBattleMoves` không ghi SD khi chưa có entry). **Cạm bẫy khi viết test phát hiện ở GĐ này**: credit một khoảng minutes lớn (nhảy thẳng level 16→26 trong 1 lần gọi) vô tình băng qua ngưỡng kiểm tra encounter (15 phút)/item (60 phút) của `applyCreditedMinutes`, khiến hàng đợi pending-event (capacity 3) bị event không liên quan (Encounter/Item ngẫu nhiên) chiếm mất slot 0 trước — `pendingEventFront()` trả về event sai, test fail khó hiểu. **Khắc phục**: đặt `totalXp` còn thiếu đúng 1 XP để lên cấp mục tiêu, rồi `creditMinutes(1, ...)` — chỉ băng qua đúng ngưỡng cấp độ cần test, không chạm ngưỡng 15/60 phút nào. Bài học cho các test tương lai liên quan tới `creditMinutes()` với khoảng nhảy cấp lớn.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,332,505 B (96.6%, còn 206,944 B ≈ 202KB)** — tăng **+2,890 B** so với GĐ6 (6,329,615 B), nhỏ vì phần lớn hạ tầng (list rows, i18n, PendingEvent queue) đã có sẵn từ các giai đoạn trước.

### GĐ 8 — Hoàn thiện ✅ XONG (commit `ddef31b0`) — giai đoạn cuối của roadmap chiến đấu

- [x] Rà soát i18n: không cần thêm chuỗi mới — mỗi GĐ trước đã thêm key ngay khi cần (GĐ5: 17, GĐ6: 8, GĐ7: 3 — tổng 28, ít hơn ước lượng ~20 ban đầu nhờ tái dùng chuỗi có sẵn). Chạy `gen_i18n.py --strip-unused` để kiểm tra: **65 key "never used" là baseline có từ trước dự án chiến đấu này** (xác nhận qua verbose log — không key nào thuộc namespace `STR_POKEMON_*` mới), không phải rác do nhánh này để lại. Không cần strip gì.
- [x] `clang-format`: máy build **không có `clang-format-21`** (CI dùng bản này) — chỉ tìm được **clang-format 22.1.3** bundle theo VSCode C++ extension (`ms-vscode.cpptools`), không cài được bản đúng vì `sudo` cần xác thực tương tác (ràng buộc y hệt vụ `libsdl2-dev` ở GĐ4/5). Vì bản 22 format khác bản 21 ở một số quyết định wrap dòng dài (xác nhận qua thử áp toàn bộ rồi so sánh: các file gốc CrossInk có từ trước nhánh này — `PokemonGame.cpp`, `PokemonActivity.cpp`, `PokemonTypeChart.cpp` — bị đề xuất sửa nhiều dòng **không liên quan gì tới chiến đấu**, dấu hiệu lệch phiên bản chứ không phải lỗi thật), đã **giới hạn phạm vi**: format toàn bộ cho các file thuần mới của nhánh này (`PokemonBattle.*`, `PokemonBattleTypes.h`, `PokemonBattleStoreCodec.cpp`, file test), còn các file cũ có sẵn từ CrossInk (`PokemonGame.cpp`, `PokemonActivity.cpp/.h`, `PokemonTypes.h`, `PokemonService.cpp`) chỉ sửa tay từng đoạn xác định chắc chắn là code mới thêm ở GĐ0-7, không đụng phần code gốc đã qua CI thật (bản 21) từ trước — tránh biến code đang sạch CI thành lệch định dạng vì chạy nhầm phiên bản. **Bài học cho phiên sau nếu còn sửa file chung với code CrossInk gốc**: nếu không có đúng `clang-format-21`, chỉ format tay phần mình viết, đừng chạy `-i` toàn file.
- [x] Build sạch cả 3 env để đối chiếu: `default` (không Pokémon) = **6,261,393 B / 95.5%**; `pokemon-x3` = **6,332,505 B / 96.6%, còn 206,944 B ≈ 202KB**; `pokemon-simulator-X3` build thành công, chạy thử khởi động 15s không có log lỗi/crash. **Tổng chi phí flash toàn bộ hệ thống chiến đấu (GĐ0→8)**: 6,332,505 − 6,303,483 (baseline trước khi bắt đầu) = **+29,022 B** — nằm gọn trong ngân sách ước lượng ban đầu (~45-65KB), rẻ hơn dự tính vì phần lớn dữ liệu/engine (GĐ1-2) gần như miễn phí (linker loại bỏ code chưa ai gọi) cho tới khi UI (GĐ5+) thực sự dùng tới.
- [x] 19/19 suite test native pass (không đổi so với GĐ7 — GĐ8 chỉ format, không sửa logic).

**Kết quả mục Verification** (đánh số theo danh sách dưới đây):
1. ✅ Build pass, delta đã tính ở trên.
2. ✅ Test native cho engine — đầy đủ (damage, khắc chế type, 6 status, catch rate 4 loại bóng, học chiêu theo learnset) từ GĐ2, cộng thêm test cho MoveLearn/teachMove/peekBattleMoves ở GĐ7.
3. ⚠️ **Chưa làm được** — cần chơi thử tương tác thật trên simulator (gặp wild→battle→ném bóng→bắt→xem Summary 4 chiêu→dùng TM→đánh gym→xem Badges→mở Elite Four). Môi trường build này **không có màn hình/bàn phím ảo để mô phỏng thao tác người dùng**, chỉ xác nhận được simulator khởi động không crash trong 15 giây đầu. **Cần người dùng tự chơi thử** theo đúng kịch bản này trên máy có GUI, hoặc trên thiết bị X3 thật.
4. ✅ Migration v2→v3 — đã có unit test tự động (`PokemonStoreCodecTest.cpp`, GĐ3): state v2 load bình thường, field mới = 0.
5. ✅ Tái tạo file phụ khi mất/hỏng — đã có unit test tự động (`PokemonBattleStoreTest.cpp`/`PokemonBattleStoreCodecTest.cpp`, GĐ3): file thiếu hoặc CRC sai được coi là rỗng, không lỗi.
6. ✅ Chống ngõ cụt (hồi HP/PP + xóa status khi đọc) — đã có unit test tự động (`PokemonServiceTest.cpp::ReadingCreditHealsAnExistingBattleEntryAndClearsStatusOnceFull`, GĐ4).
7. ⚠️ **Chưa làm được** — cần thiết bị X3 thật (tốc độ refresh e-ink mỗi lượt đánh, heap chật). Máy build này không có thiết bị nối tới; xem [pokemon-x3-build-and-flash.md](pokemon-x3-build-and-flash.md) để tự flash và test khi có thiết bị.

**Kết luận**: toàn bộ 8 giai đoạn của roadmap chiến đấu đã hoàn thành về mặt code + test tự động hóa được. Còn lại đúng 2 việc chỉ con người/thiết bị thật mới làm được (mục 3 và 7) — không phải thiếu sót của kế hoạch, mà là giới hạn vốn có của môi trường build không-tương-tác, không-thiết-bị này.

---

## Cạm bẫy khi sửa `PokemonActivity.cpp`

Thêm **một** giá trị vào `enum class Screen` phải cập nhật **~15 điểm**, nhiều chỗ là chuỗi `if` mà compiler không nhắc:

- `isListScreen()` (`:163`) — **mặc định mọi screen mới bị coi là list screen**; màn Battle phải được loại ra.
- `logicalCount()` (`:167`) — trả 0 thì `loop()` thoát sớm (`:551`) và màn hình **mất hoàn toàn điều hướng lên/xuống**.
- `Screen::Menu` là list 7 dòng hardcode ở **3 nơi phải sửa đồng bộ**: count (`:178`), dispatch theo index (`:301-323`), nhãn (`:603-615`).
- `goBack()` (`:488`) có `default:` nhảy thẳng về Menu → sub-screen phải khai báo case riêng.
- Còn lại: `activate()`, `buildRows()`, `listTop()`, `selectedRecordId()`, `renderFocused()`, `renderRowArt()`, `renderHeaderAndHints()`, và **2 chuỗi `artRows` trùng lặp** ở `:717` và `:915`.
- `FreeInkApp<24,8>` giới hạn 24 interaction / 8 handler; hiện dùng 1 handler + tối đa 10 row. `interactionOverflowed()` chưa được kiểm ở đâu cả.
- Mỗi `render()` là xóa toàn màn + `FAST_REFRESH` toàn panel + N lần mở SD đọc BMP (**art không hề được cache**).

---

## Cách build và test

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"   # pio KHÔNG có trong PATH mặc định
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive          # chỉ cần nếu freeink-sdk trống

pio run -e pokemon-x3            # firmware X3/X4 có Pokémon — số flash ở cuối log
pio run -e pokemon-simulator-X3 -t run_simulator # chạy thử trên máy tính
python3 scripts/fetch_pokemon_battle_data.py     # làm mới CSV (có cache, nhanh)
```

## Verification

1. `pio run -e pokemon-x3` — build pass, `check_firmware_size.py` không fail; so delta với baseline **6,303,483 B**.
2. Test native cho engine: damage, khắc chế type, từng status, công thức bắt theo 4 loại bóng, học chiêu khi lên level.
3. Simulator: gặp wild → Catch mở battle → đánh → ném bóng → bắt được → Summary hiện đúng 4 chiêu; nhặt item/TM khi đọc; dùng TM dạy chiêu; Gym 1 mở còn 2-8 khóa; thắng gym 1 → huy hiệu hiện ở màn Badges; đủ 8 huy hiệu → Elite Four mở.
4. **Migration**: chạy với `pokemon-a.bin` v2 có sẵn → load bình thường, túi đồ rỗng, `battleProgress = 0`, không mất Pokémon; commit đầu tiên ghi ra v3.
5. **Tái tạo file phụ**: xóa `pokemon-battle.bin` giữa chừng → vào lại trận, chiêu được dựng lại từ learnset, HP/PP đầy, không crash.
6. **Chống ngõ cụt**: đánh cho Pokémon bị thương + dính status → đọc sách một lúc → HP/PP hồi dần, status được xóa.
7. Test trên X3 thật khi ổn định, theo checklist trong [pokemon-game.md](../pokemon-game.md) — đặc biệt tốc độ refresh e-ink mỗi lượt đánh và heap chật của X3, hai thứ simulator không kiểm chứng được.

---

## GĐ 9 — Bag phân loại theo category (ngoài roadmap gốc, theo yêu cầu người dùng sau khi GĐ0-8 hoàn thành) ✅ XONG (commit `194601a0`)

Roadmap 8 giai đoạn gốc đã xong hoàn toàn (xem GĐ8 ở trên). Người dùng chơi thử simulator xong, yêu cầu thêm: túi đồ (Bag) phải phân theo 3 category — "đồ hồi máu và status thông thường" (tự đặt tên), "đồ tiến hóa" (đá + Link Cable), "TM/HM".

- [x] `Screen::Bag` đổi nghĩa: từ màn danh sách item (GĐ7 để lại, gộp đá+TM/HM) thành màn **chọn category** (3 dòng: Evolution/Medicine/TM-HM).
- [x] `Screen::BagEvolution` (6 đá + Link Cable) và `Screen::BagMachine` (55 TM/HM) — logic y hệt GĐ1/GĐ7, chỉ tách thành màn riêng khỏi Bag.
- [x] `Screen::BagMedicine` (mới) — gộp 4 category dữ liệu `Medicine`/`StatusCure`/`PPRestore`/`Candy` làm một, đặt tên hiển thị **"Medicine"** theo đúng quy ước "bag pocket" của game Pokémon gốc (người dùng yêu cầu tự nghĩ tên phù hợp).
- [x] `PokemonService::useConsumable(recordId, itemId)` (mới) — **tính năng hoàn toàn mới**, trước GĐ9 không có bất kỳ luồng "dùng" nào cho Potion/status-cure/PP-restore/Candy (chỉ đá tiến hóa và TM có nút bấm được):
  - Medicine: hồi `effectValue` HP (cap ở maxHp) + chữa status nếu `curesAilment` khớp status hiện tại (hoặc `Ailment::All` như Full Restore chữa mọi status).
  - StatusCure: chỉ chữa status, không đụng HP.
  - PPRestore: **giản lược có chủ đích** — hồi `effectValue` PP cho **mọi** ô chiêu đang biết, không phân biệt Ether (bản gốc: hồi 1 chiêu tự chọn) với Elixir (bản gốc: hồi cả 4 chiêu) — dữ liệu `pokemon-items.csv` hiện không có field nào phân biệt hai loại này (Ether/Max Ether và Elixir/Max Elixir có `effectValue` giống hệt cặp tương ứng), nên phân biệt sẽ cần thêm dữ liệu mới hoặc suy đoán qua tên item — không đáng công sức so với lợi ích ở tính năng phụ này.
  - Candy: +1 cấp qua đúng công thức `xpRequired()`, tái dùng `queueMoveLearnIfNeeded()` (GĐ7) để không bỏ sót việc học chiêu mới khi tăng cấp bằng Candy — **nhưng không kiểm tra tiến hóa**: luật đó (`queueEvolutionAfterLevelGain`) nằm trong anonymous namespace riêng của `PokemonGame.cpp`, chỉ được gọi từ `applyCreditedMinutes()`; thay vì export thêm một hàm public chỉ để dùng một lần ở đây, tiến hóa do Candy gây ra sẽ được bắt ở lần đọc sách credit tiếp theo (rất sớm sau đó trong thực tế chơi game) thay vì trùng lặp luật ở 2 chỗ.
- [x] `bagItemIdAt()`/`bagItemCount()` (PokemonActivity.cpp) tổng quát hóa `machineItemIdAt()`/`machineItemCount()` của GĐ7 bằng con trỏ hàm predicate (`bool (*matches)(ItemCategory)`), dùng chung cho cả Medicine lẫn Machine thay vì hai vòng lặp gần như giống hệt nhau.
- [x] `ItemTarget` đổi từ dispatch theo bool `bagSelectionIsMachine_` (GĐ7) sang enum 3 giá trị `BagCategory` (Evolution/Medicine/Machine); `goBack()` từ `ItemTarget` hay từ 3 màn Bag con giờ quay đúng về màn category vừa vào (`BagEvolution`/`BagMedicine`/`BagMachine`), không còn luôn nhảy về `Bag`.
- [x] 3 key i18n mới: `STR_POKEMON_BAG_EVOLUTION`/`MEDICINE`/`MACHINES`.
- [x] Test: 5 test mới trong `PokemonServiceTest.cpp` (hồi HP + cap ở maxHp, status-cure khớp/không khớp ailment, Full Restore chữa mọi status, PP restore áp dụng cho mọi ô, Rare Candy +1 cấp và `NotApplicable` ở level 100). 19/19 suite native pass.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,333,993 B (96.6%, còn 205,456 B ≈ 200KB)** — tăng **+1,488 B** so với mốc cuối roadmap GĐ8 (6,332,505 B).

## GĐ 10 — Tên chiêu trên TM/HM + nút bên nhảy trang (ngoài roadmap gốc, theo yêu cầu người dùng) ✅ XONG (commit `060d272f`)

- [x] Dòng TM/HM trong `Screen::BagMachine` giờ hiện `"<tên item> - <tên chiêu>"` (vd `"TM01 - Mega Punch"`) thay vì chỉ tên item — đỡ phải tra cứu ngoài khi duyệt. Category Medicine không đổi vì tên item đã tự giải thích đủ.
- [x] Tách hành vi 4 nút trong `PokemonActivity::loop()`: 2 nút bên hông máy (`Button::Up`/`Down`, theo `docs/controls.md`) giờ **nhảy nguyên 1 trang** (`rowsPerPage()` dòng) mỗi lần bấm/giữ; 2 nút phía trước (`Button::Left`/`Right`) vẫn **duyệt từng dòng** như cũ (trước GĐ10, `ButtonNavigator::onNext`/`onPrevious` coi cả 4 nút này tương đương nhau — đều duyệt từng dòng). Dùng thẳng `ButtonNavigator::nextPageIndex`/`previousPageIndex` (đã có sẵn từ trước, `FileBrowserActivity` cũng dùng) — 2 hàm này tự rơi về bước từng dòng khi danh sách vừa đủ 1 trang, nên áp dụng an toàn cho **mọi** màn hình danh sách trong `PokemonActivity`, không chỉ Bag.
- [x] Không cần sửa hint bar (`renderHeaderAndHints()`): `mapLabels()` chỉ hiện nhãn cho 4 nút phía trước (Back/Confirm/Left/Right — xem `MappedInputManager::mapLabels`), hành vi Left/Right không đổi nên nhãn "Up"/"Down" hiện tại (gán cho Left/Right theo quy ước cũ của màn hình danh sách dọc) vẫn mô tả đúng.
- [x] Chỉ sửa `PokemonActivity.cpp` — không đụng `PokemonService`/`PokemonGame`/bất kỳ file lưu trữ nào, đây thuần là thay đổi UI/input.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,334,665 B (96.7%, còn 204,784 B ≈ 200KB)** — tăng **+672 B** so với GĐ9.

## GĐ 11 — Màn hình quản lý moveset (ngoài roadmap gốc, theo phản hồi người dùng) ✅ XONG (commit `a554d92f`)

Người dùng phản hồi: Pokémon đủ 4 chiêu không có cách nào chủ động học thêm/thay chiêu — luồng MoveLearn tự động (GĐ7) chỉ xuất hiện đúng lúc lên cấp và moveset đầy, không có nơi nào để chủ động vào xem/đổi.

- [x] `CollectionAction::Moveset` (mới) thêm vào `pokemon::collectionActions()`, luôn xuất hiện (party lẫn PC), ngay sau "Summary" — đặt tên khác `CollectionAction::Move` (di chuyển vị trí trong party, đã có từ trước) để tránh nhầm lẫn; nhãn UI dùng `STR_POKEMON_MOVES` ("Moves") khác `STR_POKEMON_MOVE` ("Move") đã có.
- [x] `Screen::Moveset` (mới) — liệt kê 4 ô chiêu hiện tại (tên + PP) qua `service_.peekBattleMoves()` (đọc thuần, giống Summary). Chọn 1 ô mở `Screen::MovesetPick`.
- [x] `Screen::MovesetPick` (mới) — liệt kê mọi chiêu trong `learnsetFor(speciesId)` ở cấp hiện tại trở xuống mà Pokémon **chưa biết** (lọc trùng 4 ô đang có). Chọn 1 chiêu học đè vào đúng ô đã chọn ở màn trước, qua `PokemonService::learnMoveIntoSlot()` (mới, ghi đè không điều kiện — vì list đã lọc sẵn AlreadyKnown). Nếu không còn chiêu nào để học ở cấp hiện tại → thông báo `STR_POKEMON_NO_MOVES_TO_LEARN`, không vào màn rỗng.
- [x] `learnableMoveIdAt()`/`learnableMoveCount()` (PokemonActivity.cpp, thuần) gọi thẳng `pokemon::learnsetFor()` — dữ liệu learnset là bảng tĩnh không qua storage, không cần thêm API service chỉ để đọc, giống cách `moveData()`/`speciesData()` đã được gọi trực tiếp từ UI ở nhiều chỗ khác trong file.
- [x] **Khác biệt rõ với 2 luồng học chiêu đã có**: TM/HM (GĐ9) không có picker chọn-ô-thay khi đầy (chỉ báo lỗi); MoveLearn tự động (GĐ7) có picker nhưng chỉ xuất hiện đúng lúc lên cấp. Màn Moveset mới cho chủ động vào **bất cứ lúc nào**, chọn **bất cứ ô nào** để thay bằng bất cứ chiêu nào đã unlock theo cấp — đúng tính năng người dùng cần, không phải mở rộng 2 luồng cũ.
- [x] Test: cập nhật `collectionActionsExcludeOperationsThatCannotSucceed` (thứ tự/số lượng action đổi vì thêm Moveset); 1 test mới cho `learnMoveIntoSlot`. 19/19 suite native pass.
- [x] **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,335,981 B (96.7%, còn 203,472 B ≈ 199KB)** — tăng **+1,316 B** so với GĐ10.

## GĐ 12 — Sửa 4 vấn đề sau playtest GĐ11 ✅ XONG (commit `5b9d24d2`)

Cả 4 vấn đề người dùng báo cáo (xem lịch sử) đã sửa, theo đúng thứ tự đề xuất ở cuối phiên trước (3 → 2 → 1 → 4):

### 1. Đội hình gym leader dùng moveset thật của Pokémon Red

- `GymTeamMember` (`PokemonBattleTypes.h`) thêm trường `moves` (`std::array<uint8_t, GYM_MOVE_SLOTS>`, hằng số mới đồng bộ với `BATTLE_MOVE_SLOTS` qua `static_assert` vì 2 header không include lẫn nhau được).
- Lấy dữ liệu thật qua **WebFetch trực tiếp từng trang Bulbapedia** (Pewter/Cerulean/Vermilion/Celadon/Fuchsia/Saffron/Cinnabar/Viridian Gym + Lorelei/Bruno/Agatha/Lance) — không suy đoán từ trí nhớ. `pokemon-gyms.csv` đổi format team member từ `species:level` sang `species:level:move1-move2-move3-move4`; `generate_pokemon_gyms.py` parse/validate (id hợp lệ, không trùng chiêu)/emit thêm.
- Nhân tiện sửa 5 chỗ **level sai** phát hiện khi đối chiếu Bulbapedia (dữ liệu species/level cũ phần lớn đã đúng, chỉ Elite Four lệch nhẹ): Cloyster 56→53, Lapras 54→56, Bruno's Machamp 56→58, Agatha's Golbat 55→56, Agatha's Arbok 56→58.
- `PokemonActivity::setupBattleOpponent()` thêm tham số `std::span<const uint8_t> fixedMoves = {}` — gym/Elite Four truyền `team[x].moves` thẳng vào thay vì gọi `defaultMovesetForLevel()`; gặp hoang dã không đổi (fixedMoves rỗng → path cũ).
- Test: `PokemonBattleDataTest.cpp` xác nhận Brock's team đúng chiêu thật + mọi gym Pokémon phải có ít nhất 1 chiêu khác 0.

### 2. TM/HM giờ lọc đúng theo hệ Pokémon Red thật

- **Nguyên nhân đã xác định từ phiên trước**: `pokemon::canLearnViaMachine()` đã có sẵn, đúng, có test riêng, chỉ thiếu bước gọi trong `PokemonService::teachMove()`. Đã nối vào — check `AlreadyKnown` chạy TRƯỚC check tương thích (Pokémon học chiêu đó qua cách khác thì không bị luật TM chặn ngược).
- `TeachMoveOutcome` thêm `Incompatible`; UI hiện `STR_POKEMON_CANNOT_LEARN_MACHINE` khi gặp.

### 3. Màn Moveset có "Forget"; TM đầy 4 chiêu không còn chặn hẳn

- `PokemonService::forgetMove(recordId, slot)` (mới) — xóa hẳn 1 ô, từ chối nếu đó là chiêu cuối cùng còn lại (không bao giờ để Pokémon 0 chiêu). `Screen::MovesetPick` thêm dòng "Forget" cuối danh sách.
- `teachMove()` thêm tham số `replaceSlot` (mirror `resolveMoveLearn()` của GĐ7) — khi moveset đầy, UI mở `Screen::TmReplaceSlot` (mới, 4 ô hiện tại + Cancel) để chọn ô thay, gọi lại `teachMove(..., replaceSlot)` thay vì chỉ báo lỗi chặn hẳn như GĐ9 để lại.

### 4. AI đối thủ bớt máy móc, không lặp lại đúng 1 chiêu

- `chooseOpponentMove()` (`PokemonBattle.cpp`) không còn hoàn toàn xác định: 1/4 lượt cân nhắc **mọi** chiêu còn PP (không chỉ chiêu hiệu quả nhất); khi nhiều chiêu hòa điểm hiệu quả, chọn ngẫu nhiên giữa chúng thay vì luôn lấy slot đầu tiên như cũ.
- Kết hợp với mục 1 (gym Pokémon giờ có ≥2-4 chiêu thật thay vì có thể chỉ 1 chiêu tự tổng hợp) — giải quyết trực tiếp hiện tượng "Onix chỉ đánh 1 chiêu".
- Test mới: 12 seed ngẫu nhiên khác nhau phải thấy cả 2 chiêu hòa điểm hiệu quả được dùng (không phải luôn cùng 1 slot).

**Kết quả chung**: 19/19 suite native pass. Flash `pokemon-x3` (clean rebuild): **6,340,863 B (96.8%, còn 198,592 B ≈ 194KB)** — +4,882 B so với GĐ11 (nhảy lớn hơn thường lệ vì dữ liệu moveset gym thật + logic AI + 2 luồng UI mới).

**Bug phát hiện ngay sau khi commit, qua chơi thử thật** (commit `f2187062`): "Forget" ở màn Moveset chỉ đặt `moves[slot] = 0` tại đúng ô được chọn, để lại "lỗ hổng" nếu ô đó không phải ô cuối cùng còn dùng — `validateBattleRecordEntry` yêu cầu chiêu phải xếp liền từ đầu mảng (như `PokemonState::partyRecordIds`), nên `upsertEntry()` từ chối ghi trừ khi forget đúng ô cuối (chỉ đúng 25% trường hợp, im lặng thất bại 75% còn lại — chỉ có `LOG_ERR`, không có message lỗi lên UI vì trả `StorageError` giống các lỗi lưu trữ khác). Đã sửa: dồn (shift) toàn bộ chiêu sau ô bị xóa lên 1 vị trí. **Bài học**: mọi nhánh mới trong service cần test đơn vị riêng ngay khi viết — GĐ12 ban đầu thiếu hẳn test cho `forgetMove()`, chỉ dựa vào smoke-test build/khởi động giả lập (không phát hiện được bug hành vi vì đó không phải lỗi biên dịch/crash) nên bug lọt qua tới tận khi người dùng bấm thử thật. Đã bổ sung 2 test cho `forgetMove()` (forget ô giữa dồn đúng mảng; từ chối xóa chiêu cuối cùng).
