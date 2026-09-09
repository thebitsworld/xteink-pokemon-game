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
| Dữ liệu chiến đấu ở **file phụ**, không nới record | Xem ràng buộc 3. HP/PP/status **tái tạo được 100%** (suy từ level, đầy máu) → hỏng file = dựng lại, không mất Pokémon. ~~Moveset cũng tái tạo được nên file phụ không cần double-buffer~~ **không còn đúng từ GĐ11/12** (học/quên chiêu chủ động, dạy TM) — GĐ15 thêm double-buffer cho đúng file phụ này vì lý do đó |
| Dùng **base stat thật** của 151 loài | Không có stat riêng thì mọi Pokémon cùng level đánh y hệt nhau, type matchup thành yếu tố duy nhất. Chỉ tốn ~755 byte |
| ~~Gym team rút còn 2-3 con~~ **giữ nguyên đội hình đầy đủ thật** (tới 5 con) — quyết định lại ở GĐ14, đảo ngược lại quyết định ban đầu này | Ban đầu lo ngại mỗi lượt đánh = 1 lần refresh e-ink toàn màn nên cắt bớt; người dùng yêu cầu giữ đúng đội hình gốc, chấp nhận trận đấu dài hơn |
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

---

## GĐ 13 — Đổi Pokémon trong trận (party switch) ✅ XONG (commit `950d72ce`)

Trước đây gym battle và bắt Pokémon hoang dã chỉ cho đúng Pokémon đầu party ra đánh — hết HP là thua ngay dù party còn Pokémon khác đủ máu. Giờ chỉ thua khi **toàn bộ party hết HP**, hoặc player chủ động **RUN**.

- [x] `battlePartySlot_` (mới) thay cho hardcode `snapshot_.party[0]` ở `setupBattlePlayer()`/`savePlayerBattleEntry()` — `setupBattlePlayer()` nhận tham số `slot` tường minh.
- [x] `enterBattle()`/`enterGymBattle()` gọi `firstUsablePartySlot()` (mới) để bắt đầu trận với Pokémon **đầu tiên còn HP>0** thay vì luôn giả định `party[0]` sống — hết Pokémon đủ máu thì hiện `STR_POKEMON_NO_USABLE_POKEMON`, không cho vào trận.
- [x] `Screen::Battle` thêm "Switch" (FIGHT/BALL/SWITCH/RUN cho wild, FIGHT/SWITCH/RUN cho gym) → `Screen::BattleSwitch` (mới) liệt kê party còn HP>0 (trừ Pokémon đang đánh).
- [x] Khi Pokémon đang đánh gục (`OpponentWon`): **không kết thúc trận ngay** — còn Pokémon khác đủ máu thì bắt buộc vào `Screen::BattleSwitch` (`forcedBattleSwitch_`, Back bị chặn không cho hủy — giống game gốc); hết mới thật sự thua như cũ.
- [x] `usablePartySlotCount()`/`usablePartySlotAt()` (mới) dùng `service_.peekBattleMoves()` (đọc thuần, không ghi SD) để kiểm tra HP từng thành viên.
- [x] **Giản lược có chủ đích**: đổi Pokémon **không tốn lượt** (không cho đối thủ đánh miễn phí như game gốc) — `stepBattle()` không có khái niệm "switch action", thêm cơ chế lượt-chiếm-dụng riêng sẽ phải sửa engine (đã ổn định, có test riêng) chỉ để mô phỏng đúng luật này; đổi lấy sự đơn giản, vẫn giải quyết đúng vấn đề chính (thua sai điều kiện).
- [x] 4 key i18n mới: `STR_POKEMON_SWITCH`, `STR_POKEMON_NO_OTHER_USABLE`, `STR_POKEMON_NO_USABLE_POKEMON`, `STR_POKEMON_GO`.
- [x] Chỉ sửa `PokemonActivity.cpp/.h` — không đụng `PokemonService`/`PokemonBattle`/storage.
- [x] 19/19 suite native pass (không đổi service/engine nên không cần test mới ở tầng đó). Flash `pokemon-x3` (clean rebuild): **6,342,451 B (96.8%, còn 196,992 B ≈ 192KB)** — +1,556 B so với bản vá `forgetMove` trước đó.

---

## GĐ 14 — Khôi phục đội hình đầy đủ cho gym leader/Elite Four (ngoài roadmap gốc, theo yêu cầu người dùng) ✅ XONG (commit `9d433ad0`, `05b623ed`)

Sau khi xem danh sách đội hình gym/Elite Four hiện tại (yêu cầu trước đó), người dùng yêu cầu bỏ quyết định GĐ12 "rút gọn còn 2-3 con/đội vì chi phí refresh e-ink" và **giữ đúng đội hình thật của Pokémon Red**, chấp nhận trận đấu dài hơn (Giovanni/Lorelei/Bruno/Agatha/Lance đều có 5 con thật).

- [x] Rà lại toàn bộ 12 đội gym/Elite Four so với dữ liệu Bulbapedia đã lấy ở GĐ12 (đủ trong ngữ cảnh phiên, không cần fetch lại) — viết lại hoàn toàn `scripts/data/pokemon-gyms.csv` với đội hình đầy đủ: Koga 4 con, Sabrina 4, Blaine 4, Giovanni 5, Lorelei 5, Bruno 5, Agatha 5, Lance 5 (Brock/Misty vốn đã đủ 2, Surge/Erika vốn đã đủ 3 — không đổi).
- [x] Mọi species id và move id được xác nhận bằng `grep`/script Python đọc thẳng `pokemon-kanto-v2.csv`/`pokemon-moves.csv`, không dùng trí nhớ — kể cả các species trước đây bị cắt (Rhyhorn, Dugtrio, Venomoth, Rapidash, Slowbro, Jynx, Hitmonchan, Haunter, Dragonair thứ hai).
- [x] `MAX_GYM_TEAM_SIZE` (`PokemonBattleTypes.h`) 3→5, cùng `MAX_TEAM_SIZE` trong `generate_pokemon_gyms.py` và câu `PROVENANCE` (phải khớp dòng đầu CSV).
- [x] Xác nhận `parse_team()` không có ràng buộc "không được trùng species giữa các thành viên" (chỉ chặn trùng chiêu trong cùng 1 thành viên) — nên các cặp trùng loài thật của game gốc (2× Koffing/Koga, 2× Onix/Giovanni, 2× Dragonair/Lance, 2× Gengar/Agatha) không cần sửa code để parse được.
- [x] Xác nhận không cần sửa UI: `enterGymBattle()`/`advanceGymOpponentOrFinish()` đã lặp động theo `team.size()`, không có chỗ nào giả định cố định 2-3 thành viên.
- [x] **Bug phát hiện khi đối chiếu lại dữ liệu (không phải do người dùng báo)**: đội Bruno có 1 thành viên gán species id 107 (Hitmonchan) nhưng lại mang moveset thật của Hitmonlee (Jump Kick/Focus Energy/Hi Jump Kick/Mega Kick) — lỗi nhầm species id khi viết CSV ở GĐ12 (hai loài Hitmon tên rất giống nhau). Sửa riêng thành commit `9d433ad0` (107→106) trước khi gộp vào bản viết lại toàn bộ CSV, giờ cả Hitmonlee (106) và Hitmonchan (107) đều xuất hiện đúng là 2 thành viên riêng với moveset thật của từng con.
- [x] Test: thêm 5 `CHECK` trong `PokemonBattleDataTest.cpp` khẳng định `gymTeamFor(8..12).size() == 5` (Giovanni/Lorelei/Bruno/Agatha/Lance).
- [x] Cập nhật bảng "Quyết định thiết kế" — gạch quyết định GĐ12 cũ, ghi rõ đã đảo ngược ở GĐ14.
- [x] 19/19 suite native pass. Build `pokemon-simulator-X3` sạch + smoke test không lỗi. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,342,555 B (96.8%, còn 196,896 B ≈ 192KB)** — +104 B so với GĐ13 (chỉ tăng kích thước mảng cố định `GymTeamMember[5]` và dữ liệu team thêm vào, không thêm logic mới).

---

## GĐ 15 — Double-buffer cho `pokemon-battle.bin` (ngoài roadmap gốc, theo yêu cầu người dùng) ✅ XONG

Người dùng chỉ ra đúng một giả định nền tảng của GĐ3 đã không còn đúng: file phụ lưu HP/PP/moveset trận đấu (`pokemon-battle.bin`) được thiết kế **không double-buffer** vì "tái tạo được 100% từ level + learnset". Nhưng từ GĐ7 (học chiêu tự động khi lên cấp, chọn ô) và đặc biệt GĐ11/GĐ12 (màn Moveset chủ động học/quên chiêu, dạy TM/HM) trở đi, **moveset thật của một Pokémon là lựa chọn của người chơi**, không còn suy ra được từ level một cách xác định — mất file này giờ không chỉ "reset về đầy máu" mà có thể **xóa mất lựa chọn học chiêu thật của người chơi**. Ràng buộc bất di bất dịch #3 (xem đầu roadmap) vì vậy cần cập nhật: giả định "dữ liệu chiến đấu luôn tái tạo được nên không cần double-buffer" chỉ còn đúng cho HP/PP/status, không còn đúng cho moveset.

- [x] `PokemonBattleStoreCodec.h/.cpp`: thêm header 10 byte (magic `PKBT` + version + `entryCount` + `sequence` non-zero) trước phần entries — `encodeBattleStoreFile()`/`decodeBattleStoreFile()` đổi chữ ký để nhận/trả thêm `sequence`; CRC32 giờ phủ cả header lẫn entries (trước chỉ phủ entries). Giữ lại `decodeLegacyBattleStoreFile()` (logic cũ, không header) chỉ để đọc file định dạng cũ lúc migrate.
- [x] `PokemonBattleStore.h/.cpp`: đổi từ 1 file sang 2 file luân phiên `pokemon-battle-a.bin`/`pokemon-battle-b.bin`, đúng triết lý `PokemonStore` (main save) — mỗi lần ghi luôn nhắm vào file **đang không active**, đọc lại để xác minh (so khớp `sequence` + state) rồi mới chuyển con trỏ active; file đang active không bao giờ bị đụng tới nên một lần ghi/crash dở dang không thể làm mất bản còn lại. `load()` so `sequence` (có xử lý wraparound giống `PokemonStore::sequenceIsNewer`) để chọn file mới hơn trong 2 file hợp lệ.
- [x] **Migration tự động**: nếu cả 2 file mới chưa tồn tại (build cũ hơn, hoặc cài mới), `load()` thử đọc file đơn cũ `pokemon-battle.bin` bằng codec cũ (`decodeLegacyBattleStoreFile`) — nếu hợp lệ, ghi ngay ra `pokemon-battle-a.bin` (sequence 1) để có bản double-buffer bảo vệ ngay từ phiên đầu tiên sau khi cập nhật, **không xóa file cũ** (giữ làm bản khôi phục dự phòng, giống cách `PokemonStore` xử lý tên file legacy `pokemon-v2-{a,b}.bin`).
- [x] API công khai (`load()`/`findEntry()`/`upsertEntry()`/`removeEntry()`) **không đổi** — `PokemonService` và toàn bộ 40+ test hiện có dùng `PokemonBattleStore` qua constructor injection, không cần sửa gì ở tầng service/UI.
- [x] Cập nhật `docs/file-formats.md`: tách mục `pokemon-battle-{a,b}.bin` thành "Version 2 (double-buffered)" (định dạng mới) và "Version 1 (single file, superseded)" (định dạng cũ, chỉ còn ý nghĩa cho migration); sửa đoạn ở mục save chính (v3) từng khẳng định file phụ "fully-reconstructible" — không còn đúng.
- [x] Test: `PokemonBattleStoreCodecTest.cpp` thêm test cho header (magic/version/entryCount/sequence, sequence=0 bị từ chối, version lạ bị từ chối) + test riêng cho `decodeLegacyBattleStoreFile`. `PokemonBattleStoreTest.cpp` thêm: ghi liên tiếp phải luân phiên đúng A→B→A; **làm hỏng file mới hơn (B) phải fallback về file cũ hơn (A) không mất dữ liệu** (test quan trọng nhất — đúng lý do double-buffer tồn tại); cả 2 file hỏng vẫn trả rỗng chứ không crash; ghi thất bại (giả lập `setFailWritableOpen`) không đụng tới file đang active lẫn state trong bộ nhớ; migrate từ file legacy giữ đúng move đã dạy (không suy ra được từ learnset) và không xóa file legacy.
- [x] 19/19 suite native pass. `clang-format` áp cho toàn bộ file mới sửa (không đụng code CrossInk gốc nào). Build `pokemon-simulator-X3` sạch, smoke test 12s không lỗi/crash mới. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,344,073 B (96.8%, còn 195,376 B ≈ 191KB)** — +1,518 B so với GĐ14, cho toàn bộ cơ chế double-buffer + migration.

---

## GĐ 16 — Vẽ lại màn Battle theo phong cách Pokémon Red (ngoài roadmap gốc, theo yêu cầu người dùng) ✅ XONG

Người dùng chê thẳng: "màn hình battle xấu quá... tốt nhất là giống màn hình battle của Pokémon Red". Màn hình cũ (từ GĐ5) chỉ xếp chồng dọc 2 khối giống hệt nhau (tên+level, thanh HP, art) cho đối thủ rồi tới player, không có bố cục, không viền, không giống game gốc chút nào.

- [x] Viết lại hoàn toàn `renderBattleHud()` (`PokemonActivity.cpp`) theo đúng bố cục chéo kinh điển của Pokémon Red: **hộp tên/level/thanh HP của đối thủ ở góc trên-trái + art đối thủ ở góc trên-phải; art player ở góc dưới-trái + hộp tên/level/HP của player ở góc dưới-phải** — đúng cảm giác "đường chéo" đặc trưng của game gốc dù layout thiết bị là dọc (528×792) chứ không phải ngang như Game Boy.
  - Hộp tên/HP dùng `drawRoundedRect` (bo góc nhẹ, giống khung thoại/HUD bo tròn của Red) thay vì chữ + thanh trần không viền như trước.
  - Nhãn "**HP**" in đậm đặt ngay trước thanh máu, đúng vị trí kinh điển trong Red (trước đây không có nhãn này).
  - **Chỉ hộp của player hiện số HP dạng "hiện tại/tối đa"** — đúng như Red thật (không bao giờ lộ HP chính xác của Pokémon đối thủ, chỉ thấy thanh máu). Hộp đối thủ trước đây lộ số HP y hệt hộp player — không đúng tinh thần game gốc.
  - Trạng thái (PAR/SLP/FRZ/BRN/PSN/CNF) đặt cùng hàng với số HP/thanh máu thay vì hàng riêng, tiết kiệm chiều cao.
  - Toàn bộ tọa độ tính **động** theo `listBounds_.y` (đỉnh của menu FIGHT/BALL/SWITCH/RUN, vốn đã neo đáy màn hình từ GĐ5) thay vì số pixel cứng như code cũ — tự thích ứng đúng dù `rowCount_` đổi (3 dòng khi đấu gym vs 4 dòng khi gặp hoang dã đổi độ cao vùng HUD) mà không cần biết trước.
- [x] **Hộp thoại nhật ký trận đấu** (mới): khung bo góc riêng ngay phía trên menu FIGHT/BALL/SWITCH/RUN, đúng vị trí "text box" của Red — chữ căn trái (không phải căn giữa như code cũ, Red luôn căn trái), có `truncatedText()` chặn tràn nếu dòng log quá dài cho hộp.
- [x] **Thêm lời thoại mở màn** (trước đây `battleLog_` để trống tới khi đánh xong lượt đầu — màn hình trống trơn lúc mới vào trận, không giống Red): `enterBattle()`/`enterGymBattle()` giờ đặt sẵn `"Go, <Pokémon của bạn>!"` (dùng lại `STR_POKEMON_GO` có sẵn từ GĐ13); trận đấu gym còn thêm `"<tên leader> sent out <Pokémon>!"` trước đó (chuỗi mới `STR_POKEMON_SENT_OUT`). `advanceGymOpponentOrFinish()` (khi trainer tung Pokémon tiếp theo trong đội) cũng hiện đúng câu này thay vì màn hình trống.
- [x] 1 key i18n mới: `STR_POKEMON_SENT_OUT: "%s sent out %s!"`.
- [x] Chỉ sửa `PokemonActivity.cpp` + `english.yaml` — không đụng `PokemonService`/`PokemonBattle`/storage/test native (file UI này vốn không nằm trong test suite native, chỉ verify qua build thật + smoke test giả lập, như các GĐ UI trước).
- [x] Build `pokemon-simulator-X3` sạch (`rm -rf .pio/build/pokemon-simulator-X3` trước khi build vì có sửa `english.yaml` — đúng gotcha đã ghi từ GĐ5), smoke test 12s không lỗi/crash. 19/19 suite native pass (không đổi vì không chạm code có test). **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,345,083 B (96.8%, còn 194,368 B ≈ 190KB)** — +1,010 B so với GĐ15.
- [x] **Đã xác nhận bằng mắt** — người dùng tự chạy giả lập, phản hồi qua 2 vòng chỉnh (giữ tên/nickname; khung HP hẹp/cao hơn + bar căn giữa + hộp log thu nhỏ, xem mục GĐ17), sau đó xác nhận "đẹp rồi".

---

## GĐ 17 — Chấm tròn còn sống trong Battle + HP bar cho Party/Summary (ngoài roadmap gốc, theo phản hồi người dùng sau khi xem thử GĐ16) ✅ XONG (commit `470120d1`)

Người dùng chơi thử màn Battle mới (GĐ16), yêu cầu thêm 3 việc:

1. **Battle**: hiện số Pokémon còn sống mỗi bên bằng chấm tròn (chấm đặc = còn sống, chấm nhỏ đè dấu X = đã bị hạ) + **thu nhỏ khung HP**.
2. **Screen::Party**: bổ sung HP bar + HP text + status vào từng dòng để biết Pokémon nào cần dùng item hồi phục.
3. **Screen::Summary**: bổ sung HP bar + HP text.

- [x] **Khung HP thu nhỏ**: `panelHeight` 76→50, bỏ hẳn dòng tên loài (sprite bên cạnh đã đủ nhận diện) — chỉ còn 2 dòng: "HP" + bar + Level (hàng 1), "hiện tại/tối đa" + status nếu có (hàng 2). Bỏ luôn phân biệt "chỉ player mới hiện số HP" của GĐ16 — giờ cả hai bên đều hiện số theo đúng yêu cầu người dùng.
- [x] **Hàng chấm tròn** phía trên mỗi khung HP: GfxRenderer không có API vẽ hình tròn riêng — dùng `fillRoundedRect`/`drawRoundedRect` với `cornerRadius = size/2` (bo tròn hết cỡ một ô vuông = hình tròn). Chấm đặc (`fillRoundedRect` đen) = còn đánh được; chấm rỗng (`drawRoundedRect` viền) + 2 đường chéo (`drawLine`) tạo dấu X = đã gục.
  - Đối thủ: đấu gym tính theo `gymTeamFor(gymChallengeIndex_)` + `gymChallengeTeamProgress_` (thành viên có index nhỏ hơn progress = đã hạ; đúng index = đang đánh, sống hay không theo `battleOpponent_.currentHp`; lớn hơn = chưa tung ra, mặc định còn sống); gặp hoang dã chỉ 1 chấm.
  - Player: tính theo `snapshot_.partyCount`, đọc HP qua `battlePlayer_.currentHp` cho Pokémon đang đánh (tránh đọc lại từ store có thể chưa đồng bộ) và `service_.peekBattleMoves()` (đọc thuần, không ghi SD — đúng pattern `usablePartySlotAt()` từ GĐ13) cho các Pokémon còn lại trong party.
- [x] **`Screen::Party`**: dòng cao hơn (64→96px) **chỉ riêng màn này** qua `rowHeightForScreen()` (mới, dùng chung cho cả `buildList()` lẫn `rowsPerPage()` — trước đó 2 nơi hardcode `64` độc lập nhau). Xác nhận qua đọc thẳng `FreeInkUIGfxRenderer::text()`: label/value của list widget luôn **căn giữa theo chiều cao dòng** (`y = rect.y + (rect.height - lineHeight) / 2`) — nghĩa là dòng cao hơn chỉ đẩy khối chữ đã có xuống giữa, chừa khoảng trống đều ở trên **và dưới**; vẽ HP bar ở dải dưới cùng, canh theo khoảng cách cố định từ **mép dưới dòng** (không phải từ đỉnh) nên an toàn với mọi chiều cao dòng thật, không cần biết chính xác line-height của font. `renderPartyRowHealth()` (mới, gọi từ `renderRowArt()`) vẽ bar + "hiện tại/tối đa" + status abbrev.
- [x] **`Screen::Summary`**: thêm 1 dòng "HP" + bar + "hiện tại/tối đa" ngay dưới dòng Number/Level/Gender, dùng chung style field căn phải đã có sẵn cho Type/Exp/Met.
- [x] Cả 3 chỗ dùng chung `service_.peekBattleMoves()` + `pokemon::battleMaxHp(baseStatsFor(...)->hp, levelForXp(...))` để tính HP tối đa — không có field `maxHp` lưu sẵn trong `BattleRecordEntry`, phải tính lại từ base stat + level mỗi lần, đúng cách các màn khác (BattleSwitch, `usablePartySlotAt()`) đã làm từ GĐ13.
- [x] Chỉ sửa `PokemonActivity.cpp/.h`. 19/19 suite native pass (không đổi — file UI này không nằm trong test suite native). Build `pokemon-simulator-X3` sạch + smoke test 12s không lỗi. **`pio run -e pokemon-x3`** (clean rebuild): Flash **6,346,113 B (96.8%, còn 193,344 B ≈ 189KB)** — +1,030 B so với GĐ16.
- [x] **Đã xác nhận bằng mắt** — người dùng chạy giả lập, qua 2 vòng chỉnh (xem 2 mục "Sửa" ngay dưới) rồi xác nhận "đẹp rồi. tạm thời dừng ở đây".

**Sửa ngay sau khi chơi thử (commit `9b12bae6`)**: khung HP thu nhỏ ban đầu bỏ hẳn tên loài (lý do "sprite đã đủ nhận diện") — người dùng phản hồi ngay là sai ý, khung HP vẫn cần hiện tên/nickname. Thêm lại dòng tên+Level ở trên cùng (Level dời từ hàng thanh HP lên đây), `panelHeight` 50→72. Player ưu tiên nickname thật (`snapshot_.party[battlePartySlot_].nickname`) trước khi fallback `speciesName()`, đúng convention dùng ở Party row/Summary. **Bug đi kèm phát hiện khi sửa**: `panelHeight` tăng khiến dots+panel (92) giờ cao hơn sprite (90) — code cũ giả định sprite luôn cao nhất mỗi bên (đúng lúc panelHeight=50) nên tính `messageY`/`playerZoneTop` thẳng từ `spriteH`, không còn đúng nữa. Sửa bằng `zoneContentHeight = max(spriteH, dotRowHeight + panelHeight)` dùng chung, không giả định bên nào cao hơn. Flash **6,346,205 B (96.8%, còn 193,248 B)** — +92 B so với bản trước.

**Sửa lần 2 (commit `188b005d`)**: theo phản hồi tiếp — khung HP nên **hẹp lại theo chiều ngang, giãn ra theo chiều dọc**; HP bar **căn giữa trên-dưới**; HP text đặt **ngay sau HP bar** (cùng hàng, không phải hàng riêng); hộp log trận đấu **thu nhỏ lại** để nhường diện tích cho khung cảnh trận đấu + khung HP. `panelWidth` cố định 220 (trước tính động, gần hết chiều rộng màn hình) + `panelHeight` 72→92. Gộp hàng "cur/max HP"+status vào cùng hàng với bar (HP text vẽ ngay sau mép phải bar, status xa hơn về bên phải), hàng này có `rowY` tính để **căn giữa theo chiều dọc** trong khoảng trống dưới dòng tên/level thay vì cố định offset như trước. Hộp log đổi từ "chiếm hết phần còn lại" sang **chiều cao cố định 90px** neo đáy trên menu; khoảng trống giải phóng dồn vào khe hở giữa vùng đối thủ/player (`zoneGap`, tính động, sàn tối thiểu 16) thay vì bỏ phí. Flash **6,346,287 B (96.8%, còn 193,168 B)** — +82 B.

---

## Kế hoạch 3 giai đoạn tiếp theo (GĐ18-20, ghi lại 2026-09-09, CHƯA triển khai)

Ba yêu cầu mới của người dùng, cố tình **chỉ lập kế hoạch ở đây, chưa viết code** — tách riêng để mỗi giai đoạn triển khai gọn trong 1 phiên, tránh hết token giữa chừng. Đã khảo sát code thật (không đoán) trước khi ghi — số liệu/tên hàm/offset dưới đây đã xác nhận qua đọc trực tiếp source, không phải suy đoán.

### GĐ 18 — Dùng item (Bag) giữa trận đấu ✅ XONG + ĐÃ XÁC NHẬN (commit `719fd9e2` + 6 bản sửa/mở rộng: `e1233cd8`, `e7c0968e`, `02fe4a15`, `6a014052`, `b808335d`, `d278b161`)

**Vấn đề**: `Screen::Battle` hiện chỉ có FIGHT/BALL/SWITCH/RUN (hoang dã) hoặc FIGHT/SWITCH/RUN (gym) — không có cách nào dùng Potion/thuốc giải status giữa trận. `PokemonService::useConsumable()` (đã có từ GĐ9) chỉ được gọi từ `Screen::BagMedicine`, một màn hình ngoài trận, không có đường vào từ `Screen::Battle`.

- [x] Thêm dòng "BAG" vào menu `Screen::Battle` — FIGHT/BALL/BAG/SWITCH/RUN (hoang dã, 5 dòng) hoặc FIGHT/BAG/SWITCH/RUN (gym, 4 dòng — BAG dùng được cả 2 loại trận, khác BALL chỉ hoang dã). Sửa `logicalCount()` (3/4→4/5), label switch, và selection handler (index tính tương đối theo `isGym`, không hardcode).
- [x] `Screen::BattleBag` (mới) — liệt kê item dùng được giữa trận qua predicate mới `isBattleUsableCategory` (Medicine/StatusCure/PPRestore, **loại Candy**) — tái dùng `bagItemIdAt()`/`bagItemCount()` sẵn có (đã tổng quát hóa qua con trỏ hàm predicate từ GĐ9).
- [x] Chọn 1 item trong `Screen::BattleBag`: gọi `service_.useConsumable()` + `service_.consumeBagItem()` như bình thường (không thêm API mới ở `PokemonService`), rồi **đồng bộ lại `battlePlayer_`** bằng cách gọi lại `service_.peekBattleMoves()` ngay sau đó và copy `currentHp`/`status`/`statusTurns`/`moves[].currentPp` vào `battlePlayer_` — đúng phát hiện đã ghi ở bản kế hoạch: `useConsumable()` ghi thẳng `BattleRecordEntry` trên đĩa qua `recordId`, không tự đụng vào `battlePlayer_` (RAM) mà `stepBattle()`/`renderBattleHud()` thực sự đọc.
- [x] Loại `Candy` khỏi phạm vi (tăng level giữa trận kéo theo tính lại `maxHp` sống của `battlePlayer_`, phức tạp hơn hẳn) — Candy vẫn chỉ dùng được ngoài trận qua `Screen::BagMedicine` như cũ.
- [x] **Giản lược có chủ đích, nhất quán tiền lệ GĐ13** (đổi Pokémon không tốn lượt): dùng item giữa trận cũng **không tốn lượt** — không cho đối thủ đánh miễn phí, tránh phải sửa engine `stepBattle()` đã ổn định chỉ vì tính năng này.
- [x] 1 key i18n mới: `STR_POKEMON_USED_ITEM` ("%s used %s!") — hiện trong hộp log trận đấu sau khi dùng item, đúng phong cách message các hành động khác trong trận. Tiêu đề màn `Screen::BattleBag` tái dùng thẳng `STR_POKEMON_BAG` có sẵn, không cần key riêng.
- [x] Chỉ sửa `PokemonActivity.cpp/.h` + `english.yaml` — không đụng `PokemonService`/storage/test native (đúng pattern các GĐ UI-only trước, GĐ13/16/17).
- [x] 19/19 test native pass (không đổi). Build simulator sạch + smoke test không lỗi. `pio run -e pokemon-x3` (clean rebuild): Flash **6,347,087 B (96.8%, còn 192,368 B)** — +800 B so với GĐ17.
- [x] **Đã xác nhận bằng mắt** — người dùng tự chạy giả lập qua nhiều vòng phản hồi (xem các mục "Sửa"/"Mở rộng" bên dưới: giữ tên/nickname, khung HP hẹp/cao hơn, chọn mục tiêu dùng item, HP trong ItemTarget, lượt đi công bằng + HP ở BattleSwitch, menu 2 cột, sửa tràn màn hình FIGHT), cuối cùng xác nhận "đẹp rồi. tạm dừng ở đây".

**Sửa ngay sau khi chơi thử (commit `e1233cd8`)**: danh sách `Screen::BattleBag` tràn lên đè khung HUD trận đấu, không nhìn được item ở trên. Nguyên nhân: `BattleBag` bị xếp cùng nhóm "neo đáy, đè lên HUD" với `BattleMoves`/`BattleBalls` — nhóm đó an toàn vì tối đa 4 dòng, luôn vừa khoảng trống dưới HUD; nhưng `BattleBag` liệt kê tối đa 17 item (mọi id Medicine/StatusCure/PPRestore bất kể sở hữu hay không), `rowsPerPage()` không biết gì về HUD nên trả về nhiều dòng hơn hẳn 4, đẩy đỉnh danh sách lên đè HUD. Sửa: bỏ `BattleBag` khỏi cả `bottomAnchored` (`buildList()`) lẫn điều kiện vẽ HUD (`renderFocused()`) — giờ dùng list toàn màn hình canh trên xuống như mọi màn Bag khác, đã có sẵn phân trang đúng cho danh sách dài. Đánh đổi: không còn thấy HUD phía sau khi chọn item, chấp nhận được. Flash không đổi (thuần sửa layout).

**Sửa lần 2 (commit `e7c0968e`)**: chọn item trong `Screen::BattleBag` không dùng được. Nguyên nhân thật: bản đầu tự động áp thẳng lên `battlePartySlot_` (Pokémon đang đánh), bỏ qua việc chọn mục tiêu — không đúng luồng chơi thật (một Pokémon dự bị đang dưỡng thương cũng cần hồi máu được, không chỉ Pokémon đang đánh). Sửa: thêm `BagCategory::BattleMedicine` (mới), `Screen::BattleBag` giờ chỉ chọn item rồi chuyển sang `Screen::ItemTarget` (tái dùng nguyên màn chọn Pokémon đã có sẵn cho luồng ngoài trận, không thêm màn mới) — khác luồng Medicine thường ở chỗ dùng xong quay về `Screen::Battle` thay vì `Screen::Party`, và chỉ đồng bộ `battlePlayer_` (RAM) nếu mục tiêu đúng là Pokémon đang đánh (Pokémon dự bị không tham gia `stepBattle()`/`renderBattleHud()` lúc này nên không cần đồng bộ gì). Flash **6,347,199 B (96.9%, còn 192,256 B)** — +112 B.

**Sửa lần 3 (commit `02fe4a15`)**: màn chọn mục tiêu (`Screen::ItemTarget`) khi dùng item Medicine/BattleMedicine cần hiện thêm HP bar, HP/HP text, status hiện tại của từng Pokémon — đúng thông tin cần để quyết định dùng cho ai. Thêm `itemTargetShowsHealth()` (true khi `bagCategory_` là `Medicine`/`BattleMedicine`, false cho `Evolution`/`Machine` vì đá tiến hóa/TM không có HP để hiện) — tái dùng y hệt cơ chế "dòng cao hơn (96px) + dải HP bar ở đáy dòng" đã làm cho `Screen::Party` ở GĐ17 (`rowHeightForScreen()` và điều kiện gọi `renderPartyRowHealth()` giờ dùng chung điều kiện này), không thêm hàm vẽ mới. Flash **6,347,283 B (96.9%, còn 192,160 B)** — +84 B.

**Mở rộng lần 4 (commit `6a014052`)**: 3 yêu cầu tiếp theo.
1. `Screen::BattleSwitch` hiện HP bar/text/status như Party/ItemTarget (`showsPartyHealthRows()` đổi tên từ `itemTargetShowsHealth()`, mở rộng bao gồm `BattleSwitch`) — `renderRowArt()` cần ánh xạ riêng qua `usablePartySlotAt()` vì index của màn này không phải party slot trực tiếp.
2. **Đổi Pokémon hoặc dùng item giữa trận giờ tốn nguyên 1 lượt** — đảo ngược giản lược "không tốn lượt" đã chọn ở GĐ13/bản đầu GĐ18, theo đúng luật Gen 1: cả hai hành động luôn giải quyết tức thì (không so Speed), rồi đối thủ được đánh trả ngay. Engine thêm `stepOpponentOnlyTurn()` (mới, `PokemonBattle.h/.cpp`) — chỉ chạy hành động của đối thủ, không so Speed (người chơi không đưa hành động nào ra để so), vẫn áp dụng end-of-turn status damage như bình thường. Tách `chooseOpponentMoveSlot()` và `finishTurn()` ra khỏi lambda nội bộ cũ của `stepBattle()` thành hàm dùng chung giữa 2 hàm — xác nhận refactor không đổi hành vi bằng cách chạy 19/19 test cũ pass **trước khi** thêm test mới. `PokemonService::resolveOpponentOnlyTurn()` (wrapper mỏng, đúng pattern `resolveBattleTurn()`). UI gọi hàm này ngay sau khi đổi Pokémon tự nguyện (không phải bị ép đổi sau khi gục) hoặc dùng item, xử lý outcome giống hệt `Screen::BattleMoves`.
3. Thứ tự ra đòn khi cả 2 bên dùng FIGHT vẫn theo Speed như cũ (đúng từ GĐ2, không đổi) — phần mới chỉ là switch/item không tham gia so Speed vì luôn giải quyết trước, đúng game gốc.
- Test: 4 test mới `PokemonBattleTest.cpp` (không so Speed dù player nhanh hơn, có thể hạ gục player, vẫn tick status cuối lượt, short-circuit khi đã gục sẵn) + 1 test `PokemonServiceTest.cpp` (thin-wrapper).
- 19/19 test pass. Build simulator sạch + smoke test không lỗi. Flash **6,349,315 B (96.9%, còn 190,128 B)** — +2,032 B.

**Mở rộng lần 5 (commit `b808335d`)**: sau khi thêm BAG, màn Battle hoang dã (5 dòng FIGHT/BALL/BAG/SWITCH/RUN) tràn/đè lên khung HUD phía trên — khung HUD (GĐ16/17) chưa từng tính tới trường hợp menu 5 dòng (lúc đó tối đa 4). Người dùng yêu cầu luôn: cả battle gym lẫn battle bắt Pokémon hoang dã nên chia menu lệnh thành 2 cột cho đỡ tốn diện tích. Viết hẳn `renderBattleMenu()` (mới) vẽ tay lưới nút 2 cột thay cho danh sách 1 cột chung (`fui::list()`) — giảm số dòng thật cần xuống `ceil(N/2)` (hoang dã 5→3, gym 4→2), giải quyết tràn màn hình tận gốc bằng cách giảm chiều cao thay vì chỉnh lại hằng số HUD. `Screen::Battle` loại khỏi `isListScreen()` (BattleMoves/BattleBalls/BattleBag/BattleSwitch không đổi — tên chiêu/item/Pokémon dài không hợp lưới 2 cột hẹp). `battleMenuTop()` (mới) tính đỉnh lưới theo số dòng thật, `renderBattleHud()`'s `hudBottom` dùng hàm này thay `listBounds_.y`. `loop()` thêm nhánh điều hướng riêng cho màn này: Left/Right vẫn ±1 theo thứ tự đọc, Up/Down nhảy ±2 (đúng 1 cột) thay vì "nhảy trang" (vô nghĩa với lưới 2 cột). Nút chọn tô đen chữ trắng, nút thường viền đen chữ đen. **Biết trước, chấp nhận được**: lưới mới không đăng ký vùng chạm (touch) qua `app_`/`fui` như list chung — X3 (thiết bị chính) không có cảm ứng nên không ảnh hưởng, chỉ là khoảng trống nếu test bằng chuột/chạm trên giả lập/thiết bị khác thay vì nút bấm. Flash **6,350,749 B (96.9%, còn 188,704 B)** — +1,434 B.

**Sửa ngay sau khi chơi thử (commit `d278b161`)**: bấm FIGHT, danh sách chiêu (`Screen::BattleMoves`) đè lên khung text HUD. Nguyên nhân: `hudBottom` đổi sang `battleMenuTop()` cho **mọi** screen dùng chung `renderBattleHud()` (Battle/BattleMoves/BattleBalls), nhưng `battleMenuTop()` gọi `logicalCount()` — hàm này trả số khác nhau tùy `screen_` (số lệnh cho `Battle`, số chiêu cho `BattleMoves`) nên tính sai đỉnh vùng dành cho HUD khi đang ở `BattleMoves`. Sửa: chỉ dùng `battleMenuTop()` khi `screen_ == Battle`; `BattleMoves`/`BattleBalls` quay lại dùng `listBounds_.y` như trước (2 màn này vẫn là list thường qua `buildList()`, không đổi ở lần mở rộng 5). Flash **6,350,777 B (96.9%, còn 188,672 B)** — +28 B.

### GĐ 19 — Bóng trong túi đồ ngoài trận ✅ XONG (commit `f0b49554`)

**Đã xác nhận qua code, không cần sửa**: rớt bóng qua đọc sách + `dropWeight` phân tầng theo độ hiếm (Poke 40 > Great 20 > Ultra 8 > Master 1) đã đúng từ GĐ1/GĐ4; bóng đã bị khóa hoàn toàn ở gym battle (gate theo `isGym` ở cả label switch lẫn selection handler của `Screen::Battle`) đã đúng từ trước.

- [x] `Screen::BagBalls` (mới) — liệt kê 4 loại bóng + số lượng, tái dùng đúng cách `Screen::BattleBalls` đọc (`itemData(EVOLUTION_ITEM_COUNT+1+index)` + `bagCounts[index]`) — chỉ xem, không action khi Activate (hiện `STR_POKEMON_BAG_BALLS_INFO` giải thích bóng chỉ dùng được khi bắt Pokémon hoang dã).
- [x] `Screen::Bag` từ 3 lên 4 dòng: Evolution/Medicine/Balls/TM-HM.
- [x] 2 key i18n mới: `STR_POKEMON_BAG_BALLS` ("Balls"), `STR_POKEMON_BAG_BALLS_INFO`.
- [x] **Không thêm `BagCategory::Balls`** — lệch khỏi kế hoạch ban đầu có chủ đích: `BagBalls` không đi qua luồng chọn-item-rồi-chọn-mục-tiêu (`Screen::ItemTarget`) như Evolution/Medicine/Machine, nên không cần phân biệt ngữ cảnh qua `bagCategory_` — `goBack()` map thẳng `BagBalls → Bag` như 3 màn kia, không cần thêm state.
- [x] Phòng thủ thêm (không có trong kế hoạch gốc, làm luôn vì tiện): `Screen::BattleBalls`'s handler giờ tự kiểm tra `gymChallengeIndex_ != 0` — trước đây chỉ an toàn nhờ đường vào duy nhất đã gate theo `isGym`; đáng làm vì menu Battle đã đổi cấu trúc 2 lần chỉ trong GĐ18 (thêm BAG, rồi đổi bố cục 2 cột).
- [x] Chỉ sửa `PokemonActivity.cpp/.h` + `english.yaml` — không đụng `PokemonService`/data/test.
- [x] 19/19 test native pass (không đổi). Build simulator sạch + smoke test không lỗi. Flash **6,351,221 B (96.9%, còn 188,224 B)** — +444 B so với GĐ18.
- [x] **Đã xác nhận bằng mắt** — người dùng chạy giả lập, xác nhận "ok rồi" sau khi xem Bag > Balls.

**Mở rộng theo phản hồi (commit `fe283cb3`)**: ẩn item số lượng 0 khỏi **mọi** menu túi đồ, không riêng Balls — `BagEvolution`/`BagMedicine`/`BagBalls`/`BagMachine` (ngoài trận) và `BattleBag`/`BattleBalls` (giữa trận) đều lọc. `bagItemIdAt()`/`bagItemCount()` (Medicine/Machine/BattleBag) nhận thêm `bagCounts`, lọc ngay lúc duyệt category. `ownedSlotAt()`/`ownedSlotCount()` (mới, 2 bản nạp chồng uint8_t/uint16_t) cho 2 màn đánh index trực tiếp vào mảng cố định (BagEvolution's `itemCounts` 6 phần tử, Balls' 4 phần tử đầu `bagCounts`) — map "item thứ N đang sở hữu" ngược về vị trí thật. **Bug phát hiện khi làm (không phải lỗi cũ, phát sinh do chính thay đổi này)**: `renderRowArt()`'s icon cho `BagEvolution` vẫn dùng thẳng row index làm species đá — sau khi lọc sẽ vẽ sai đá nếu không map qua `ownedSlotAt()` trước, đã sửa cùng lúc. Thêm guard "0 bóng tổng thì không cho vào `BattleBalls`" (đúng pattern `BattleBag` đã có) và empty-state `STR_POKEMON_BAG_EMPTY` khi cả category rỗng (đúng pattern `Screen::Pc`'s "No Pokémon are stored"). Flash **6,351,659 B (96.9%, còn 187,792 B)** — +438 B.

### GĐ 19.5 — Tự xuống dòng cho text thông báo + battle log (ngoài roadmap gốc, theo phản hồi người dùng) ✅ XONG (commit `db8e87c8`)

Người dùng phản hồi sau khi chơi thử: text thông báo trong gym battle (`Screen::Message`) tràn màn hình khi cầm dọc máy; text trong battle log (hộp thoại trong `renderBattleHud()`) cũng có vẻ tràn. Yêu cầu: mọi loại text này tự xuống dòng khi vượt kích cỡ màn hình, tìm phương án hiển thị đẹp hơn.

- [x] `Screen::Message` — `message_` là câu bất kỳ tối đa 96 ký tự (VD "None of your Pokémon can battle right now!"), trước đây vẽ thẳng qua `centered()` (chỉ hiểu đúng 1 dấu `\n` có sẵn, không tự wrap câu dài không có `\n`) — đủ dài để tràn quá mép màn hình dọc (528px ở 12pt bold). Sửa: word-wrap qua `renderer.wrappedText()` trước, vẽ từng dòng đã wrap bằng `centered()`.
- [x] Hộp log trận đấu (`renderBattleHud()`) — trước đây mỗi nửa dòng (hành động player/đối thủ) dùng `truncatedText()` (cắt bớt kèm "..." nếu quá rộng, không xuống dòng). Sửa: word-wrap từng nửa qua `wrappedText()` rồi nối lại, giới hạn tổng số dòng theo chiều cao hộp để không bao giờ tràn khung. Tăng `messageHeight` 90→160 để đủ chỗ tối đa 4 dòng thay vì 2 — đã kiểm tra `zoneGap` (khoảng đệm giữa vùng đối thủ/player) vẫn không xuống dưới sàn tối thiểu 16px trong cả 2 trường hợp gym (2 dòng menu) lẫn hoang dã (3 dòng menu), do có rất nhiều khoảng trống dư ra từ khi menu Battle đổi sang 2 cột (GĐ18).
- [x] Chỉ sửa `PokemonActivity.cpp` (thêm `#include <vector>` cho kiểu trả về của `wrappedText()`). 19/19 test native pass (không đổi). Build simulator sạch + smoke test không lỗi. Flash **6,351,915 B (96.9%, còn 187,536 B)** — +256 B so với GĐ19.
- [x] **Đã xác nhận bằng mắt** — người dùng xác nhận "text wrap ok" ở phiên tiếp theo.

### GĐ 20 — Công cụ chỉnh save file giả lập (mở rộng thành tool đa năng) ✅ XONG (commit — xem bên dưới)

**Thay đổi phạm vi so với kế hoạch gốc**: kế hoạch ban đầu chỉ định viết 1 script hẹp để chèn 1 pending encounter. Người dùng sau đó yêu cầu rộng hơn: "tạo 1 tool hỗ trợ edit save file để tiện edit cập nhật thông số nếu cần" — nên viết luôn thành `scripts/dev/edit_pokemon_save.py`, một CLI đa năng với nhiều subcommand, thay vì 1 script dùng-một-lần.

- [x] `scripts/dev/edit_pokemon_save.py` (mới) — CLI dựa trên `argparse`, mặc định thao tác trên `fs_/.crosspoint` (đường dẫn tương đối từ vị trí script, override được qua `--save-dir`). Chỉ hiểu đúng format v3 hiện tại (195-byte state) — từ chối thao tác nếu gặp version khác thay vì đoán mò làm hỏng dữ liệu lạ.
- [x] Subcommand `dump` — in party (recordId/species/totalXp/caughtLevel/gender/nickname), `battleProgress` (gym + Elite Four đã hạ), item đá tiến hóa + túi đồ (chỉ hiện cái > 0), và cả 3 pending event slot. Tra tên loài/item trực tiếp từ `pokemon-kanto-v2.csv`/`pokemon-items.csv` (không hardcode bảng tên riêng, tránh lệch với data thật).
- [x] Subcommand `reset-battle-store` — xóa `pokemon-battle-{a,b}.bin` + file cũ `pokemon-battle.bin`, để HP/PP/status tự dựng lại đầy khi cần.
- [x] Subcommand `reset-gym-progress` — set `battleProgress` về 0 (xóa cả lịch sử đấu gym lẫn huy hiệu, vì đây là cùng 1 field).
- [x] Subcommand `queue-encounter --species --level [--gender] [--slot]` — chèn 1 pending event Encounter; tự chọn gender hợp lệ theo đúng luật `genderMatchesSpecies()` nếu không chỉ định (`validate_gender()`/`default_gender_for()` implement lại y hệt rule trong `PokemonTypes.cpp`); tự chọn slot rỗng đầu tiên nếu không chỉ định `--slot`, báo lỗi rõ ràng nếu cả 3 slot đều đầy.
- [x] Subcommand `set-bag-item --item --count` — set thẳng số lượng 1 item (tự nhận diện đá tiến hóa id 1-6 dùng `itemCounts` u16, hay item túi đồ id 7-83 dùng `bagCounts` u8).
- [x] Subcommand `set-record-xp --record-id --xp` — set thẳng `totalXp` của 1 record (party hoặc PC) để test ngưỡng lên cấp/tiến hóa mà không cần đọc sách.
- [x] Chấp nhận id số hoặc tên (không phân biệt hoa/thường, bỏ qua dấu gạch ngang/khoảng trắng) cho `--species`/`--item`.
- [x] Mọi lệnh ghi đều patch **cả 2** file `pokemon-a.bin`/`pokemon-b.bin` giống hệt nhau (đúng tiền lệ double-buffer), tính lại CRC32 bằng `zlib.crc32` (đã xác nhận khớp thuật toán firmware từ phiên trước). Tự động backup `.bak` trước lần ghi đầu (tắt được qua `--no-backup`); `--dry-run` xem trước không ghi gì.
- [x] Đã tự kiểm thử toàn bộ 5 subcommand ghi (dry-run rồi ghi thật) trên save thật của giả lập, xác nhận CRC hợp lệ sau mỗi lần ghi, `dump` đọc lại đúng giá trị vừa đổi, và simulator boot sạch không lỗi/corrupt sau khi tool ghi đè.
- [x] Không đụng code C++/test — thuần công cụ Python độc lập, không ảnh hưởng build/flash.
- [x] Hướng dẫn dùng đầy đủ (từng subcommand + ví dụ): [docs/development/pokemon-save-edit-tool.md](pokemon-save-edit-tool.md).

### GĐ 21 — Cân bằng lại tỉ lệ rớt item theo category (ngoài roadmap gốc, theo phản hồi người dùng) ✅ XONG (commit `8b980ea0`)

**Vấn đề phát hiện khi người dùng hỏi về tỉ lệ rớt Ball**: mô phỏng Monte Carlo cho thấy người chơi mới trung bình mất **~130 giờ đọc** (median ~92h) mới lần đầu có 1 quả bóng bất kỳ — trong khi wild encounter xuất hiện đều đặn (mỗi 15 phút roll 2/5 + bảo hiểm). Nguyên nhân gốc: `createItem()` roll **phẳng** trên toàn bộ 83 item theo `drop_weight` riêng lẻ — category nào có **nhiều loại nhất** áp đảo xác suất tổng bất kể từng item riêng có hiếm hay không. Machine (TM+HM) có 55 item (mỗi cái `drop_weight` thấp = 6/2) nhưng cộng dồn lại chiếm **310/703 ≈ 44%** tổng trọng số, trong khi Ball chỉ có 4 item (Poke/Great/Ultra/Master) cộng lại **69/703 ≈ 9.8%** — không phải vì Ball "được thiết kế hiếm" mà vì thua hẳn về *số lượng loại* so với Machine. Save mới cũng bắt đầu với 0 bóng (`bagCounts{}` khởi tạo toàn 0, không seed bóng ban đầu) và `Screen::BattleBalls` (bắt Pokémon hoang dã) yêu cầu sở hữu ít nhất 1 bóng mới vào được.

- [x] Đổi `createItem()`'s fallback roll (nhánh không có đá tiến hóa nào đang cần — nhánh `preferOwnedNeeds` giữ nguyên, không bị ảnh hưởng vì đã restricted sẵn theo nhu cầu thật) từ **1 roll phẳng trên 83 item** sang **2 bước**: `chooseItemCategory()` (mới) roll category trước theo bảng trọng số cố định `CATEGORY_DROP_WEIGHTS` (Ball=25, Stone=10, Medicine=20, StatusCure=15, PPRestore=10, Machine=15, Candy=5 — tổng 100, lọc bỏ category nào hiện không còn item nào trống chỗ qua `categoryHasAvailableItem()`), rồi mới `buildItemCandidates()` (mở rộng thêm tham số `std::optional<ItemCategory> categoryFilter`) roll item cụ thể trong category đã chọn theo đúng `drop_weight` hiện có — Poke Ball vẫn phổ biến hơn hẳn Master Ball trong nội bộ Ball, chỉ có việc "category nào được xét tới trước" là không còn phụ thuộc số lượng item nữa.
- [x] Kết quả mô phỏng sau khi đổi: Ball chiếm 25/100=25% (thay vì 9.8%) → thời gian trung bình tới quả bóng đầu tiên giảm từ ~130h xuống ước tính **~50-55h** (chưa đo lại bằng mô phỏng sau khi merge, chỉ tính theo tỉ lệ mới thay cho tỉ lệ cũ — có thể tinh chỉnh thêm `CATEGORY_DROP_WEIGHTS` nếu vẫn thấy chậm).
- [x] Cập nhật 3 test hiện có (`oneBoundaryQueuesItemEncounterAndEvolutionInOrder`, `forcedItemWithLevelGainQueuesItemBeforeEvolution`, `saturatedItemsDoNotRejectReadingCredit`) theo đúng số lần gọi random mới (category-roll rồi mới item-roll, thay vì chỉ item-roll) — các test này dùng `SequenceRandom` ghim cứng từng giá trị roll nên lộ ra ngay khi số lần gọi thay đổi (test tự báo "unexpected random draw" nếu thiếu 1 giá trị).
- [x] Thêm 1 test mới `itemCategoryWeightsGiveEachCategoryAFairShareRegardlessOfHowManyItemsItHolds` — xác nhận đúng ranh giới 2 đầu dải roll category (Ball ở đầu dải `[0,25)`, roll=24 vẫn trúng Ball rồi roll=68 trúng đúng Master Ball ở cuối dải item; Candy ở cuối dải `[95,100)`, roll=99 trúng Candy và **bỏ qua hẳn roll item-level** vì Candy chỉ có đúng 1 thành viên — xác nhận tối ưu hóa "skip roll khi chỉ có 1 lựa chọn" vẫn hoạt động đúng sau khi thêm bước category).
- [x] Chỉ sửa `lib/Pokemon/PokemonGame.cpp` + `test/pokemon_game/PokemonGameTest.cpp` — không đụng CSV/generator/service/UI. 20/20 test native pass (19 cũ + 1 mới). Flash **6,352,263 B (96.9%, còn 201,337 B)** — +1,486 B so với GĐ20.
- [x] **Chưa đủ, đã bị GĐ22 thay thế** — người dùng đánh giá "vẫn chưa ổn": gốc rễ không chỉ là tỉ lệ giữa các category mà là việc **mọi category vẫn tranh nhau đúng 1 suất rơi mỗi giờ**. Xem GĐ22 bên dưới.

### GĐ 22 — Tách 4 category thành 4 track thu thập độc lập (theo yêu cầu người dùng) ✅ XONG (commit `8c07b6b4`)

**Yêu cầu người dùng**: "hiện đang có 4 category là evolution, medicine, TM/HM và balls, thì hoạt động thu thập các item trong 4 category này là độc lập. evolution cơ chế giữ nguyên. tính toán phương án lại cho 3 category còn lại cho hợp lý. trong đó ball không quá ít để khi gặp pokemon, player vẫn có thể còn bóng mà bắt. nên cân nhắc phân phối balls theo tần suất xuất hiện của pokemon cho hợp lý."

**Vì sao GĐ21 chưa đủ**: GĐ21 mới chỉ sửa *tỉ lệ chia* giữa các category, nhưng vẫn giữ nguyên **1 suất rơi duy nhất mỗi giờ** cho tất cả. Kể cả khi Ball được nâng lên 25% suất đó, kỳ vọng vẫn chỉ ~0.02 quả bóng/giờ — quá ít so với ~1.84 lần gặp Pokémon/giờ. Đây là bài toán nhịp độ (cadence), không phải bài toán trọng số.

**Số liệu nền dùng để tính (đo/mô phỏng thật, không ước lượng)**:
- Encounter check chạy 4 lần/giờ (mỗi 15 phút + mốc giờ), mỗi lần 2/5 + bảo hiểm 3 lần trượt → **~1.84 lần gặp Pokémon/giờ**.
- Tỉ lệ bắt thành công = `catchValue/255` (`attemptCatch`, `PokemonBattle.cpp`) — Pokémon còn đầy máu chỉ ~15-33%, đánh còn nửa máu ~31-67%. Thực tế tốn **2-3 quả bóng cho mỗi con bắt được**.
- Save mới bắt đầu với **0 bóng** (`bagCounts{}` toàn 0, không seed), và `Screen::BattleBalls` yêu cầu có ít nhất 1 quả mới vào được.

- [x] **Ball — bám thẳng vào nhịp encounter**: cấp **1 quả tại mỗi lần check encounter** (cùng nhịp 15 phút) = **4 quả/giờ = 2.18 quả cho mỗi lần gặp Pokémon**, dư so với mức tiêu ~2-3 quả/lần bắt nên kho bóng tăng dần thay vì cạn. Cấp **thẳng vào túi, không tạo `PendingEvent`** — ở nhịp này bóng sẽ làm tràn hàng đợi 3 slot và bị bỏ rơi đúng lúc cần nhất; người chơi xem số lượng ở Bag > Balls (GĐ19) hoặc ngay màn ném bóng. Cấp **trước** encounter check trong cùng 1 phút, nên quả vừa nhặt dùng được luôn cho con vừa gặp.
- [x] **Loại bóng mở khóa dần theo `bookProgressPercent`** (đúng pattern `regularEncounterEligible` đã có sẵn cho encounter): `<50%` chỉ Poke Ball → `>=50%` thêm Great → `>=75%` thêm Ultra → `>=95%` thêm Master. **Master Ball chỉ rơi khi đang sở hữu 0 quả** — giữ đúng tinh thần "quân bài tẩy dùng 1 lần" của game gốc thay vì tích trữ được; trọng số trong nhóm vẫn lấy nguyên cột `drop_weight` của CSV (Poke 40 > Great 20 > Ultra 8 > Master 1), không đụng dữ liệu.
- [x] **Medicine** — gồm Medicine + StatusCure + PPRestore + Candy (khớp đúng những gì màn Bag > Medicine đang hiện, để "thuốc bạn nhặt được" trong luật rơi đúng bằng "thuốc bạn thấy trên màn hình"): roll **1/2 mỗi giờ = 1 món mỗi 2 giờ**. Rộng tay được vì HP/PP đã tự hồi khi đọc sách rồi, nên thuốc là tiện lợi chứ không phải sinh tử.
- [x] **TM/HM** — roll **1/3 mỗi giờ = 1 cái mỗi 3 giờ**. Có 55 cái để sưu tầm nên đây là nhánh dài hơi; mỗi cái vẫn mở khóa 1 chiêu vĩnh viễn nên không nên rơi quá dày.
- [x] **Đá tiến hóa — giữ nguyên y hệt** theo đúng yêu cầu: 1/20 mỗi giờ + bảo hiểm 19 lần trượt, ưu tiên đá mà Pokémon đang sở hữu thật sự dùng được (`preferOwnedNeeds`) = **1 viên mỗi ~12.4 giờ**. Đổi đúng 1 điểm: khi không con nào đang cần đá, fallback giờ chỉ mở rộng **trong nhóm Stone** thay vì tràn ra toàn bộ 83 item — chính chỗ tràn này là thứ hút hết suất rơi và làm chết nguồn cung bóng trước đây.
- [x] Bỏ hẳn `CATEGORY_DROP_WEIGHTS`/`chooseItemCategory`/`categoryHasAvailableItem` của GĐ21 (vô tác dụng khi mỗi track đã có lịch riêng). Tách `pickWeightedItem()` dùng chung cho cả 4 đường, `buildItemCandidates()` đổi tham số lọc từ `std::optional<ItemCategory>` sang con trỏ hàm predicate (đúng convention `bagItemIdAt()` bên UI đã dùng) để lọc được nhóm nhiều category như Medicine.
- [x] **Không đổi save format, không đổi `PendingEvent`, không đổi CSV** — cố ý bỏ pity counter cho 3 track mới (sẽ cần thêm field vào `PokemonState` → bump v4 + migration): ball ở nhịp 4/giờ không cần bảo hiểm, medicine/TM trượt vài lượt cũng không gây bí.
- [x] Cập nhật 10 test hiện có theo đúng số lần gọi random mới; thay 1 test GĐ21 bằng 3 test mới: ball rơi mỗi lần check mà không tạo event và không chiếm slot queue, mở khóa loại bóng theo tiến độ + Master Ball chỉ giữ 1 quả, medicine/TM roll trên 2 track tách biệt. Đổi tên + viết lại `fullQueueCreditsReadingAndPrimesGuaranteesWithoutRolling` → `fullQueuePrimesGuaranteesButStillHandsOutBalls` (queue đầy giờ chặn 3 track sinh event, nhưng ball vẫn về — đúng thiết kế).
- [x] Chỉ sửa `lib/Pokemon/PokemonGame.cpp` + `test/pokemon_game/PokemonGameTest.cpp`. 19/19 test native pass. Flash **6,352,731 B (96.9%, còn 200,869 B)** — +468 B so với GĐ21.
- [x] **Đã bị GĐ23 thay thế** — người dùng đánh giá "vẫn chưa ổn" (4 quả/giờ cấp thẳng, không có tỉ lệ rơi thật; sau khi bàn số liệu cùng nhau, chốt tỉ lệ mới ở GĐ23). Xem GĐ23 bên dưới.

### GĐ 23 — Ball/Medicine/TM-HM thành 4 track độc lập với bảo hiểm riêng (theo yêu cầu người dùng, sau khi bàn số liệu) ✅ XONG (commit `4a08c436`)

**Bối cảnh**: sau GĐ22, người dùng chỉ ra ball "4 quả/giờ cấp cứng, không có tỉ lệ rơi" là chưa ổn. Bàn qua nhiều vòng số liệu cụ thể (mô phỏng Monte Carlo mỗi phương án) trước khi chốt:
- Ball: thử 2/5 (→~1.84 quả/giờ, đúng bằng tần suất encounter — hợp lý theo lý thuyết nhưng rủi ro cạn nếu cố bắt mọi lần gặp) rồi 3/5 (→~2.47 quả/giờ, ~1.34x tần suất gặp) — **chốt 3/5 + bảo hiểm 3 lần trượt**.
- Medicine: hỏi lại "1 món/2 giờ có chậm không" — tính ra HP-item cụ thể (Potion) chỉ ~1 quả sau 10 giờ đọc, quá mỏng cho 1 trận gym 5 con. Bàn "nếu tính theo tỉ lệ encounter thì sao, giảm PP/status thêm" → **chốt 2/5 + bảo hiểm 3 lần trượt (nhịp 15 phút, không còn theo giờ), StatusCure/PPRestore giảm trọng số nội bộ còn 1/3 gốc** (Medicine/HP chiếm ~70.5% track thay vì ~46%).
- TM/HM: **chốt cùng công thức 2/5 + bảo hiểm 3 lần trượt** như Medicine (thời gian đọc để sưu tầm đủ 55 loại giảm từ ~1183 giờ xuống ~207 giờ).
- Đá tiến hóa: **giữ nguyên y hệt** theo đúng yêu cầu ban đầu ("evolution cơ chế giữ nguyên").
- Mô phỏng cuối cùng ở nhịp đọc 7 giờ/tuần: Ball ~17.2 quả/tuần, Medicine ~12.7 món/tuần (Potion ~4.3, Super Potion ~2.1...), TM/HM ~12.7 cái/tuần, đá ~0.35 viên/tuần.

- [x] **Ball** — chuyển từ "cấp thẳng mỗi lần check" sang roll thật 3/5 + bảo hiểm 3 lần trượt (field mới `ballMisses`), vẫn giữ đúng thiết kế GĐ22: không qua `PendingEvent`, cấp thẳng vào túi.
- [x] **Medicine** — chuyển từ hourly 1/2 (không bảo hiểm) sang nhịp 15 phút 2/5 + bảo hiểm 3 lần trượt (field mới `medicineMisses`). Thêm `medicineItemWeight()` — hàm nhân trọng số runtime (không đụng CSV) giảm StatusCure/PPRestore còn 1/3, chỉ áp dụng khi roll trong track Medicine.
- [x] **TM/HM** — chuyển từ hourly 1/3 (không bảo hiểm) sang nhịp 15 phút 2/5 + bảo hiểm 3 lần trượt (field mới `machineMisses`).
- [x] **Đá tiến hóa** — không đổi (vẫn 1/20/giờ + bảo hiểm 19 lần, `itemMisses`).
- [x] Refactor `rollPityGate()` (mới, dùng chung) — 1 hàm duy nhất cho "roll chance, bảo hiểm sau N lần trượt", áp dụng lại cho CẢ encounter và đá tiến hóa (vốn đã tự cài công thức này riêng lẻ trước đây) để tránh 5 chỗ code trùng lặp cùng 1 công thức.
- [x] **Save format v3(195 byte)→v4(198 byte)**: thêm 3 field `u8` mới (`ballMisses`/`medicineMisses`/`machineMisses`) sau `battleProgress`, đúng tiền lệ append-only (không đụng offset 0-194). Migration v3→v4 tự động zero-fill 3 field mới cho save cũ. Cập nhật `docs/file-formats.md` (mục Version 4) và `scripts/dev/edit_pokemon_save.py` (offset/hằng số version mới + hiển thị 3 pity counter trong `dump`).
- [x] Viết lại phần lớn `test/pokemon_game/PokemonGameTest.cpp` — nhiều test cũ dùng nhiều lần credit 15-phút liên tiếp, giờ có thể khiến ball/medicine/TM-HM cùng lúc đạt bảo hiểm y hệt encounter và cạnh tranh chung 3 slot hàng đợi; thêm helper `findEventOfKind()` để test tìm đúng event theo loại thay vì giả định cứng vị trí slot. Test `multiHourCreditUsesLifetimeAtEachHourlyBoundary` neutralize Medicine/TM-HM bằng cách max hết `bagCounts` liên quan (mọi item "sold out") để chúng không cạnh tranh slot với 3 lần encounter mà test cần theo dõi.
- [x] Chỉ sửa `PokemonGame.cpp`, `PokemonTypes.h/.cpp`, `PokemonStoreCodec.h/.cpp` + test + doc/tool liên quan. 19/19 test native pass. Flash **6,353,021 B (96.9%, còn 200,579 B)** — +290 B so với GĐ22.
- [ ] **Chưa xác nhận bằng mắt** — cần chơi thử thực tế để cảm nhận nhịp rơi bóng/thuốc/TM đã hợp lý chưa; mọi hằng số cân bằng (`BALL_CHANCE_NUMERATOR`, `ITEM_TRACK_CHANCE_NUMERATOR`, pity threshold, hệ số `/3U` trong `medicineItemWeight()`) đều nằm gọn đầu `PokemonGame.cpp`, chỉnh lại không cần đụng save format hay CSV.

### GĐ 24 — Màn chọn chiêu (FIGHT) xếp 2 cột, PP nhỏ hơn tên chiêu (theo phản hồi người dùng) ✅ XONG (commit `26e0b4e1`)

**Vấn đề**: người dùng chơi thử báo "màn hình battle khi fight và chọn move, pokemon có 4 move thì phần move sẽ bị đè vào phần battle log". Nguyên nhân: `Screen::BattleMoves` vẫn là list 1 cột qua `fui::list()` (tối đa 4 dòng × 64px = 256px), trong khi HUD/hộp log trận đấu chỉ chừa không gian cỡ 2-3 dòng menu Battle — cách tính "bottom-anchored" của `buildList()` không khớp thực tế.

- [x] Chuyển `Screen::BattleMoves` sang đúng mô hình lưới 2 cột đã dùng cho menu Battle (GĐ18 mở rộng lần 5) — `renderBattleMoveMenu()` (mới) vẽ tay, bypass `fui::list()` hoàn toàn (loại khỏi `isListScreen()`). Tái dùng thẳng `battleMenuTop()` làm điểm neo trên cùng — không cần hàm mới vì `logicalCount()` đã trả đúng `battlePlayerMoveCount()` khi màn này active, nên công thức "N item, lưới 2 cột" áp dụng được luôn. Hạ từ 4 dòng xuống còn 2 dòng, hết đè lên hộp log.
- [x] `loop()` thêm `BattleMoves` vào nhánh điều hướng lưới 2 cột đã có sẵn cho `Battle` (Left/Right ±1, Up/Down ±2 cột, wrap).
- [x] Trong mỗi nút: tên chiêu chữ đậm dòng trên (qua `truncatedText()` phòng chiêu tên dài như "Solar Beam"/"Sky Attack"), PP hiện "cur/max" chữ thường dòng dưới, căn phải — **không có font nhỏ hơn `UI_10_FONT_ID` trên máy này** (chỉ có 2 font: `UI_10`/`UI_12`), nên "PP nhỏ hơn" đạt được bằng độ đậm (không bold) + vị trí phụ (dòng dưới, căn phải) thay vì cỡ chữ thật sự nhỏ hơn.
- [x] Chỉ sửa `PokemonActivity.cpp/.h`. 19/19 test native pass (không đổi logic ngoài UI). Build simulator sạch. Flash **6,353,495 B (96.9%, còn 200,105 B)** — +474 B.
- [ ] **Chưa xác nhận bằng mắt** — cần chơi thử lại để xem layout 2 cột đã hết đè lên battle log chưa.
