# CLAUDE.md — Session Handoff Notes

Ghi chú ngữ cảnh cho Claude ở phiên làm việc sau (hoặc trên máy khác) để tiếp tục công việc mà không phải khám phá lại từ đầu. File này KHÔNG phải tài liệu chính thức của dự án — chỉ là sổ tay bàn giao, có thể dọn/xóa khi công việc đang dở đã xong.

## Dự án là gì

Đây là fork của **CrossInk** (firmware ESP32-C3 cho máy đọc sách e-ink Xteink X3/X4), được mở rộng thêm **module game Pokémon** — Pokémon lên cấp dựa trên thời gian đọc sách thật (không phải cày game thuần túy). Xem [docs/pokemon-game.md](docs/pokemon-game.md) (tài liệu người dùng) và [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) (tài liệu kỹ thuật chi tiết — **đọc file này trước** để hiểu cơ chế game).

## Việc đã làm trong phiên này (2026-09-04)

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
6. **Tạo file tài liệu kỹ thuật mới**: [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) + thêm link vào [docs/development/README.md](docs/development/README.md). **Chưa commit** — đang là working tree changes.

## Trạng thái working tree hiện tại

Chạy `git status` để xem chính xác, nhưng tính đến cuối phiên có các thay đổi CHƯA COMMIT:
- `docs/development/pokemon-mechanics.md` (file mới)
- `docs/development/README.md` (thêm 1 dòng link)
- `.pio/` có build artifacts từ `pio run -e pokemon-x3` / `-e default` (thường đã bị `.gitignore`, kiểm tra lại nếu cần)
- `freeink-sdk/` submodule đã được init (trước đó trống) — không phải thay đổi cần commit, chỉ là submodule đã checkout đúng commit ghi trong `.gitmodules`.

**Chưa commit gì trong phiên này** — người dùng chưa yêu cầu commit.

## Việc đang dở / hướng tiếp theo (do người dùng gợi mở, chưa chốt)

Người dùng muốn tìm hiểu khả năng **mở rộng game Pokémon với cơ chế kiểu Pokémon Red gốc** (hệ thống trận đấu turn-based). Đã phân tích và khuyến nghị:

- **Bản đồ họa/animation đầy đủ kiểu Red gốc: rủi ro cao** — dễ vượt quá ~230KB dung lượng flash còn lại.
- **Bản text-based (menu FIGHT/ITEM/PKMN/RUN + log text, không animation) khả thi hơn nhiều** — tái dùng `EpdFont`/`GfxRenderer`/`ButtonNavigator` đã có sẵn trong UI reader và `PokemonActivity` hiện tại. Ước lượng chi phí thêm ~20-40KB flash, lọt trong dư địa hiện có.
- **Chưa viết bất kỳ dòng code battle system nào** — mới chỉ dừng ở phân tích/ước lượng dung lượng. Nếu tiếp tục, bước hợp lý tiếp theo là:
  1. Thiết kế struct dữ liệu move/type-chart (theo phong cách `PokemonTypes.h`/`PokemonSpecies.h` hiện có).
  2. Prototype một bản tối giản (ví dụ 20-30 chiêu, type chart rút gọn) rồi **build thử ngay để đo dung lượng thật** (dùng đúng quy trình đã làm ở bước 4 trên) thay vì đoán mò trước khi đầu tư viết đầy đủ.
  3. Cẩn thận với chuỗi i18n đa ngôn ngữ cho tên chiêu/log trận đấu — dễ phình dung lượng nếu dịch nhiều thứ tiếng ngay từ đầu.

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
