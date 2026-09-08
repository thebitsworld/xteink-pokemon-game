# CLAUDE.md — Session Handoff Notes

Ghi chú ngữ cảnh cho Claude ở phiên làm việc sau (hoặc trên máy khác) để tiếp tục công việc mà không phải khám phá lại từ đầu. File này KHÔNG phải tài liệu chính thức của dự án — chỉ là sổ tay bàn giao, có thể dọn/xóa khi công việc đang dở đã xong.

## Dự án là gì

Đây là fork của **CrossInk** (firmware ESP32-C3 cho máy đọc sách e-ink Xteink X3/X4), được mở rộng thêm **module game Pokémon** — Pokémon lên cấp dựa trên thời gian đọc sách thật (không phải cày game thuần túy). Xem [docs/pokemon-game.md](docs/pokemon-game.md) (tài liệu người dùng) và [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) (tài liệu kỹ thuật chi tiết — **đọc file này trước** để hiểu cơ chế game).

## Bối cảnh đã khảo sát (phiên 2026-09-04)

1. **Đọc sâu toàn bộ module Pokémon** (`lib/Pokemon/*`, `src/pokemon/*`, `src/activities/pokemon/*`) và giải thích chi tiết cơ chế: đo thời gian đọc chống gian lận (`PokemonTracker`), vòng lặp XP/encounter/item/evolution (`PokemonGame.cpp::applyCreditedMinutes`), lưu trữ double-buffer + CRC (`PokemonStore`). Toàn bộ đã chép lại vào [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md).
2. **Phát hiện quan trọng về gameplay**: `bookProgressPercent` (dùng để gate độ hiếm/level wild encounter) là % tiến độ của **cuốn sách đang đọc hiện tại**, không phải tổng thời gian/trang đã đọc lũy kế → đọc nhiều sách ngắn đạt % cao nhanh hơn, cho encounter tốt hơn với cùng lượng thời gian đọc thật so với đọc một cuốn dài. Đây có thể là điểm cần cân bằng lại nếu muốn sửa.
3. **Đã cài đặt môi trường build thành công**:
   - PlatformIO đã cài ở `~/.platformio/penv/bin/pio` nhưng **không có trong PATH mặc định** của shell — phải `export PATH="$HOME/.platformio/penv/bin:$PATH"` trước khi chạy `pio`.
   - Submodule `freeink-sdk` **chưa init lúc bắt đầu phiên** (thư mục rỗng) → phải chạy `git submodule update --init --recursive` trước khi build (nếu không sẽ lỗi `PackageException: Can not create a symbolic link`).
4. **Build thật `pio run -e pokemon-x3` và `pio run -e default` để đo dung lượng** — số liệu thật (không phải ước lượng):

   | Build | Flash dùng | % | Free trong OTA slot (6.25MB) |
   |---|---|---|---|
   | `default` (không Pokémon) | 6,259,237 B | 95.5% | 280,208 B |
   | `pokemon-x3` (có Pokémon) | 6,303,483 B | 96.2% | 235,968 B |

   - RAM tĩnh chỉ dùng 17.7% (327,680 bytes tổng) — **không phải nút thắt**.
   - **Flash mới là nút thắt thật sự**: chỉ còn ~230KB trống trước khi `scripts/check_firmware_size.py` (post-build check trong `platformio.ini`) fail build.
   - Module Pokémon hiện tại chỉ tốn ~43KB flash (vì artwork nằm trên SD card, không đóng gói vào firmware).
5. **Đổi git remote `origin`** từ `https://github.com/padge01/xteink-pokemon-game.git` sang `https://github.com/thebitsworld/xteink-pokemon-game.git` theo yêu cầu người dùng. Đã `git fetch origin` thành công (repo tồn tại, kết nối được) nhưng **chưa kiểm tra remote mới có khác gì so với code hiện tại (branch `main` local vẫn đang track cấu hình cũ, chưa merge/rebase gì)**.
6. **Tạo file tài liệu kỹ thuật**: [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) — đã commit (`aaec0c59` trên `main`).

## Trạng thái git

- `main` có commit `aaec0c59` (tài liệu cơ chế + CLAUDE.md) — **chưa push** vì máy này chưa có credential GitHub nào hoạt động (chưa cài `gh`, SSH key `~/.ssh/id_ed25519` chưa đăng ký với GitHub, không có credential helper HTTPS).
- Remote `origin` đã đổi sang `https://github.com/thebitsworld/xteink-pokemon-game.git`. Đã `git fetch` được, nhưng **chưa đối chiếu xem remote có khác gì so với local**.
- Đang làm việc trên nhánh `feat/pokemon-battle-system` (tách từ `main`).
- Chạy `git status` để biết chính xác cái gì chưa commit.

## Việc đang dở: hệ thống chiến đấu Pokémon

Đang triển khai trên nhánh **`feat/pokemon-battle-system`**. Kế hoạch đầy đủ, ràng buộc kỹ thuật, quyết định thiết kế kèm lý do, và danh sách task chia theo giai đoạn đều nằm ở:

👉 **[docs/development/pokemon-battle-roadmap.md](docs/development/pokemon-battle-roadmap.md)** — đọc file này trước khi viết code.

Phạm vi: học chiêu theo level (dữ liệu Red thật), vật phẩm + TM/HM rơi khi đọc sách, chiến đấu turn-based đầy đủ status effect, bắt Pokémon bằng 4 loại bóng, 8 gym + Elite Four theo thứ tự, màn hình huy hiệu.

**Tiến độ**: **Toàn bộ 8 giai đoạn của roadmap chiến đấu đã xong**, cộng thêm **GĐ9-10 ngoài roadmap gốc** (commit `b4d21f31`, `eff18d13`, `f1ff1bd2`, `da111328`, `18a0eeca`, `a989d161`, `2f420118`, `5f496ad6`, `ddef31b0`, `62fd5028`, `194601a0`, `7605de43`, `060d272f` trên `feat/pokemon-battle-system`). **Push đang do người dùng tự làm thủ công** — máy này không có credential GitHub hoạt động (xem "Trạng thái git" bên dưới), đừng tự ý thử push nữa trừ khi được yêu cầu lại.

**GĐ10 (tên chiêu trên TM/HM + nút bên nhảy trang, commit `060d272f`, ngoài roadmap gốc)**: Bag>TM-HM hiện "TM01 - Mega Punch" thay vì chỉ "TM01". `PokemonActivity::loop()` tách 2 nút bên hông (Up/Down) = nhảy 1 trang (`ButtonNavigator::nextPageIndex`/`previousPageIndex`, có sẵn), 2 nút phía trước (Left/Right) = từng dòng như cũ — áp dụng cho mọi màn danh sách, không chỉ Bag. Chỉ sửa `PokemonActivity.cpp`, không đụng service/storage. Flash **6,334,665 B/96.7%, còn 204,784 B**, +672 B.

**GĐ9 (Bag phân loại category, commit `194601a0`, ngoài roadmap gốc — người dùng yêu cầu sau khi chơi thử simulator)**: `Screen::Bag` giờ là màn chọn category (Evolution/Medicine/TM-HM) thay vì gộp chung. `Screen::BagMedicine` mới + `PokemonService::useConsumable()` — **tính năng hoàn toàn mới**: trước đây Potion/status-cure/PP-restore/Candy không dùng được, chỉ có đá tiến hóa và TM. Giản lược có chủ đích: PPRestore hồi mọi ô chiêu (không phân biệt Ether/Elixir vì data không có field phân biệt); Candy tăng cấp nhưng không tự check tiến hóa (bắt được ở lần đọc sách kế tiếp). Flash **6,333,993 B/96.6%, còn 205,456 B**, +1,488 B. 19/19 test pass, thêm 5 test cho useConsumable. Chi tiết đầy đủ ở mục "GĐ 9"/"GĐ 10" cuối roadmap.

**Việc còn lại không phải code** — 2 mục Verification chỉ con người/thiết bị thật mới làm được (xem roadmap mục GĐ8 để biết chi tiết):
1. Chơi thử tương tác trên simulator có GUI thật (gặp wild→battle→bắt→xem Summary 4 chiêu→dùng TM→đánh gym→Badges→Elite Four) — máy build này không có màn hình/input ảo.
2. Test trên thiết bị X3 thật (tốc độ refresh e-ink, heap chật) — xem [pokemon-x3-build-and-flash.md](docs/development/pokemon-x3-build-and-flash.md).

**GĐ8 (hoàn thiện, commit `ddef31b0`)**: rà soát i18n xong (không cần thêm key, 65 "never used" là baseline có từ trước). `clang-format`: máy không có `clang-format-21` (CI), chỉ có v22.1.3 (bundle VSCode cpptools) — **chỉ format tay phần code do nhánh này viết** (GĐ0-7), không đụng code CrossInk gốc có từ trước để tránh làm lệch định dạng đã qua CI thật. Build sạch 3 env: `default` 6,261,393 B/95.5%; `pokemon-x3` **6,332,505 B/96.6%, còn 206,944 B**; simulator chạy được. **Tổng chi phí flash toàn bộ 8 giai đoạn: +29,022 B** so với baseline 6,303,483 B trước khi bắt đầu — rẻ hơn ước lượng ban đầu (~45-65KB).

**GĐ7 (Summary 4 chiêu + học chiêu + dạy TM, commit `5f496ad6`)**: `PendingEventKind::MoveLearn` (khai báo từ GĐ3, chưa dùng tới) giờ được sinh thật khi lên cấp và moveset đầy 4 ô (còn ô trống thì tự học, không hỏi). Summary hiện 4 chiêu qua `peekBattleMoves()` (đọc thuần, không ghi SD). Bag mở rộng gồm 55 vật phẩm Machine để dạy TM/HM (`teachMove()`) — **giản lược có chủ đích**: TM khi moveset đầy không có picker chọn-ô-thay (chỉ báo lỗi, không tiêu TM), khác với MoveLearn tự động (CÓ picker 5 dòng). Flash **6,332,505 B / 96.6%, còn 206,944 B (~202KB)**, +2,890 B so với GĐ6. 34/34 test PokemonServiceTest, 19/19 suite native tổng. **Gotcha khi viết test**: credit minutes nhảy cấp lớn trong 1 lần gọi có thể băng qua ngưỡng encounter/item (15/60 phút) làm hàng đợi pending-event bị event khác chiếm chỗ — luôn đặt `totalXp` thiếu đúng 1 XP rồi `creditMinutes(1,...)` khi test cần băng qua đúng 1 ngưỡng cấp độ cụ thể.

**GĐ6 (UI Gym List + Badges, commit `2f420118`)**: `Screen::GymList`/`Badges` + 2 mục menu. Chọn gym đang mở thực sự vào trận (tái dùng 3 màn Battle của GĐ5, không có BALL, đối thủ tung lần lượt cả đội 2-3 con, thắng hết mới `markGymDefeated`). Logic mở khóa tuyến tính gộp vào 1 hàm dùng chung `pokemon::gymProgressFor()` giữa service và UI. Flash **6,329,615 B / 96.6%, còn 209,840 B (~205KB)**, +2,816 B so với GĐ5. 19/19 test pass.

**GĐ5 (UI Battle + bắt bóng, commit `a989d161`)**: 3 màn `Battle`/`BattleMoves`/`BattleBalls` (không phải 4 — bỏ `BattleBag`, engine chưa có cơ chế dùng item giữa trận) nối vào nút Catch cũ. Flash **6,326,799 B / 96.5%, còn 212,656 B (~208KB)**. 19/19 test pass. **Gotcha quan trọng phát hiện ở GĐ này (vẫn áp dụng cho mọi GĐ sau)**: sửa `lib/I18n/translations/*.yaml` xong, `pio run` incremental có thể báo "up to date" mà KHÔNG rebuild `I18nStrings.o` (SCons không track qua `pre:` generator hook) → linker dùng object cũ với `StrId` enum layout cũ, tra sai chuỗi âm thầm không lỗi build. **Luôn `rm -rf .pio/build/<env>` sau khi sửa file yaml trong `lib/I18n/translations/`** trước khi tin số liệu build. Chi tiết đầy đủ trong roadmap mục GĐ5.

- GĐ 0: dữ liệu nguồn — `scripts/data/pokemon-{stats,moves,learnsets,tmhm}.csv` từ PokeAPI (151/165/989/3037 dòng) + `pokemon-gyms.csv` viết tay (8 gym + 4 Elite Four).
- GĐ 1: 5 generator C++ sinh header vào `$BUILD_DIR/generated/pokemon`, cộng struct thủ công `lib/Pokemon/PokemonBattleTypes.h` + 5 file accessor `.cpp`.
- GĐ 2: `lib/Pokemon/PokemonBattle.h/.cpp` (engine thuần: damage, 6 status, catch) + `lib/Pokemon/PokemonTypeChart.cpp` (bảng khắc chế 18×18 viết tay). Sửa 1 bug dữ liệu thật: `ailment_chance=0` của PokeAPI cho chiêu Status nghĩa là "chắc chắn 100%", không phải "không bao giờ".
- GĐ 3: save format v3 (`bagCounts[77]` + `battleProgress` append sau byte 115) + file phụ `/.crosspoint/pokemon-battle.bin` (`PokemonBattleStoreCodec` + `PokemonBattleStore`, không double-buffer vì tái tạo được 100%). `PendingEventKind::MoveLearn` mới.
- GĐ 4: `PokemonService` thêm `consumeBagItem`/`markGymDefeated`/`loadBattleEntry`/`saveBattleEntry` + hồi HP/PP khi đọc trong `creditMinutes()`. `createItem()` (PokemonGame.cpp) đổi sang chọn theo trọng số trên 83 vật phẩm — **thay đổi duy nhất** chạm PokemonGame.cpp.
- **Flash**: GĐ1/2 = 6,303,587 B (dữ liệu/engine chưa ai gọi, linker loại bỏ hết) → GĐ3 = 6,304,033 B (+446, state đổi thật) → **GĐ4 = 6,314,621 B (96.4%, còn 224,832 B ≈ 220KB), +10,588 B** vì service giờ thực sự gọi vào toàn bộ bảng dữ liệu. Đây là mốc chi phí thật đầu tiên đúng như roadmap dự đoán.
- 19/19 native test Pokémon pass, 25/25 trong `PokemonServiceTest` (`cd test && ctest -R Pokemon`, cần cmake của PlatformIO: `export PATH="$HOME/.platformio/packages/tool-cmake/bin:$PATH"` vì máy không cài cmake hệ thống).
- **Chưa chạy được simulator**: máy thiếu `libsdl2-dev`, không cài được vì `sudo` cần xác thực tương tác (không có trong môi trường non-interactive này). Nếu phiên sau có quyền sudo tương tác, `sudo apt-get install -y libsdl2-dev` rồi `pio run -e pokemon-simulator-X3 -t run_simulator` để kiểm chứng runtime thật.
- **Lưu ý cho GĐ5+ (UI)**: firmware hiện tại build được nhưng **chưa có màn hình nào dùng API mới** — service layer đã sẵn sàng nhưng vô hình với người chơi cho tới khi UI (GĐ5-7) nối vào.

**Bốn ràng buộc dễ gây hỏng nhất** (chi tiết trong roadmap):
1. Flash chỉ còn ~230KB — build đo lại sau mỗi giai đoạn.
2. `PokemonState` chỉ được append **sau byte 115** (`PokemonStore.cpp:265` hardcode offset 108 cho `sequence`).
3. **Không** nới `PokemonRecord` 48 byte — dữ liệu chiến đấu để ở file phụ tái tạo được.
4. Tên chiêu/vật phẩm **không** đi qua i18n (bảng offset nhân 28 ngôn ngữ + `strip_unused` không quét `$BUILD_DIR`).

## Cách build nhanh (đã verify hoạt động trong phiên này)

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive   # chỉ cần nếu freeink-sdk trống
pio run -e pokemon-x3    # firmware có Pokémon, cho X3/X4
pio run -e default       # firmware gốc, không Pokémon — dùng để so sánh dung lượng
pio run -e simulator      # build cho simulator (test nhanh, không cần thiết bị)
```

Số liệu Flash/RAM thực xuất hiện ở cuối log build (`RAM:`, `Flash:` percentages) và trong output của `check_firmware_size.py`.

### Chạy native test suite (nhanh hơn build firmware nhiều, không cần thiết bị)

Máy này **không có `cmake` hệ thống**, nhưng PlatformIO có bundle sẵn:

```sh
export PATH="$HOME/.platformio/penv/bin:$HOME/.platformio/packages/tool-cmake/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game/test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # lần đầu sẽ fetch googletest, hơi lâu
cmake --build build -j4                          # build toàn bộ target (hoặc --target <Tên> cho 1 cái)
cd build && ctest -R "Pokemon" --output-on-failure
```

`test/build/` đã có trong `.gitignore` (`build` — dòng 11).
