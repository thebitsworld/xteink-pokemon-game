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

### GĐ 3 — Lưu trữ

- [ ] `PokemonState` v3: append `bagCounts[N]` (uint8, stack ≤99) và `uint16_t battleProgress` (8 bit gym + 4 bit Elite Four) **sau byte 115**. `itemCounts[6]` cũ giữ nguyên offset 54..65.
- [ ] `snapshotStateBytes()` + `decodeState()` thêm nhánh v2 (field mới = 0). Migration chạy tự động vì commit luôn ghi version hiện tại.
- [ ] `validateState()` ràng buộc `battleProgress` — **phải thỏa mãn với state v2 zero-extend**, nếu không save cũ thành không đọc được.
- [ ] `PendingEventKind::MoveLearn`: dùng lại đúng 10 byte sẵn có (`recordId` = Pokémon, trường `speciesId` chứa moveId, `level` = level học) → chỉ thêm case vào `validatePendingEvent()`, không đổi kích thước.
- [ ] `src/pokemon/PokemonBattleStore.h/.cpp`: file phụ 16 byte/entry (`recordId(4) + moves[4] + pp[4] + currentHp(2) + status(1) + statusTurns(1)`) + CRC32. Tạo lazy; CRC sai hoặc thiếu entry → **dựng lại từ learnset, HP/PP đầy**, không bao giờ chặn người chơi.
- [ ] Test migration native theo mẫu `test/pokemon_store/PokemonStoreTest.cpp` (xem test `legacySnapshotMigratesToV2...` làm mẫu).

### GĐ 4 — Service + rơi đồ  ⟵ mốc đo dung lượng đầu tiên

- [ ] `PokemonService`: API túi đồ (thêm/bớt/đọc), đọc/ghi chiêu qua battle store, đọc/ghi tiến trình gym. Mẫu sẵn có: `renamePokemon()`/`setEvolutionPrompts()` cho Replace-một-record, `movePartyMember()` cho state-only.
- [ ] Hồi phục khi đọc trong `PokemonService::creditMinutes()` — nơi duy nhất có sẵn cả lối gọi game lẫn quyền truy cập file phụ. `PokemonGame.cpp` **không** bị đụng ở đây.
- [ ] Mở rộng `createItem()` trong `PokemonGame.cpp` từ 6 đá tiến hóa sang bảng ~80 vật phẩm có trọng số. Đây là thay đổi **duy nhất** chạm vào `PokemonGame.cpp`.
- [ ] **`pio run -e pokemon-x3`** → so với baseline 6,303,483 B. Đủ data+engine+storage+service mà chưa có UI, nên biết được phần "nền" tốn bao nhiêu trước khi đầu tư viết UI.

### GĐ 5 — UI: Battle + bắt bằng bóng

- [ ] Thêm `Screen::Battle`, `BattleMoves`, `BattleBag`, `BattleBalls`.
- [ ] Nối vào nhánh Encounter của `Screen::Event` (`PokemonActivity.cpp:434-448`): "Catch" mở màn Battle thay vì bắt ngay; ném bóng thành công mới gọi `service_.resolveEncounter(Catch, ...)`. "Pass" giữ nguyên.
- [ ] Vẽ tự do trong `renderFocused()`. Dùng `GfxRenderer::wrappedText()` (đã có sẵn, UTF-8-safe, hiện chưa dùng trong file này) cho log; `fillRect`/`drawRect` cho thanh HP; `drawPokemonSpeciesArt` cho ảnh tĩnh.

### GĐ 6 — UI: Gym List + Badges

- [ ] Thêm `Screen::GymList`, `Screen::Badges` + 2 mục vào `Screen::Menu`.
- [ ] Hiển thị trạng thái từng gym: đã thắng / đang mở / còn khóa; Elite Four khóa tới khi đủ 8 huy hiệu.

### GĐ 7 — UI: 4 chiêu ở Summary + học/thay chiêu

- [ ] Khối 4 chiêu (tên + PP) trong `renderFocused()` nhánh Summary, pitch 26px. Portrait còn ~350px trống; **landscape chật hơn nhiều** (chỉ ~11 dòng tổng, khối tiến hóa đã tới y≈253) nên cần rút gọn.
- [ ] Giữ Summary **chỉ đọc** (`logicalCount()` vẫn trả 0) để không đổi ngữ nghĩa input hiện tại.
- [ ] Luồng resolve `PendingEventKind::MoveLearn`: học chiêu mới khi lên level, chọn chiêu bỏ nếu đã đủ 4; dùng TM/HM dạy chiêu.

### GĐ 8 — Hoàn thiện

- [ ] ~20 chuỗi i18n, **chỉ thêm vào `lib/I18n/translations/english.yaml`** rồi chạy `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n`.
- [ ] `./bin/clang-format-fix` (CI dùng clang-format 21).
- [ ] Chạy đủ mục Verification dưới đây.

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
