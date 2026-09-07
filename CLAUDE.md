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

**Tiến độ**: GĐ 0 và GĐ 1 đã xong (commit `b4d21f31`, `eff18d13` trên `feat/pokemon-battle-system`, đã push). Còn GĐ 2-8 (engine chiến đấu thuần → lưu trữ → service → UI → hoàn thiện) — **đọc mục "GĐ 2" trong roadmap và bắt đầu từ đó**.

- GĐ 0: dữ liệu nguồn — `scripts/data/pokemon-{stats,moves,learnsets,tmhm}.csv` từ PokeAPI (151/165/989/3037 dòng) + `pokemon-gyms.csv` viết tay (8 gym + 4 Elite Four).
- GĐ 1: 5 generator C++ (`scripts/generate_pokemon_{moves,stats,learnsets,items,gyms}{,_build}.py`) sinh header vào `$BUILD_DIR/generated/pokemon`, cộng struct thủ công `lib/Pokemon/PokemonBattleTypes.h` + 5 file accessor `.cpp`. Build thật đo được: **Flash chỉ tăng +104 byte** (6,303,483 → 6,303,587) vì chưa có code nào gọi tới các hàm tra cứu — dữ liệu bị linker loại bỏ hết cho tới khi dùng thật. 16/16 native test Pokémon pass (`cd test && ctest -R Pokemon`, cần cmake của PlatformIO: `export PATH="$HOME/.platformio/packages/tool-cmake/bin:$PATH"` vì máy không cài cmake hệ thống).

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
