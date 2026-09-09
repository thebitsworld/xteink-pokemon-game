# Tool chỉnh save file Pokémon (`scripts/dev/edit_pokemon_save.py`)

Công cụ dev-only để xem/chỉnh tay save file Pokémon dưới sandbox SD-card của simulator (`fs_/.crosspoint` mặc định), phục vụ test nhanh (xếp sẵn wild encounter, hồi full HP/PP/status, xóa lịch sử gym, gán item vào túi, set XP...) mà không phải chờ gameplay/RNG thật. Không phải tính năng sản phẩm.

Chỉ hiểu đúng **save format hiện tại của nhánh này** (main save version 3, state 195 byte) — gặp format lạ sẽ từ chối chạy thay vì liều ghi đè có thể hỏng.

**An toàn khi ghi**: mỗi lần ghi luôn vá cả `pokemon-a.bin` lẫn `pokemon-b.bin` (nếu cả hai tồn tại) giống hệt nhau, đúng quy ước double-buffer của chính firmware — nên dù lần boot sau chọn file nào làm "active" cũng đúng. Tự tạo bản `.bak` cho mỗi file trước lần ghi đầu tiên trong phiên chạy, trừ khi truyền `--no-backup`.

## Chuẩn bị

Đóng simulator trước khi chỉnh save (tránh ghi đè lẫn nhau giữa tool và tiến trình simulator đang chạy). Chạy từ thư mục gốc repo:

```sh
python3 scripts/dev/edit_pokemon_save.py <lệnh> [tham số]
```

Mặc định tool tự tìm `fs_/.crosspoint/pokemon-{a,b}.bin`. Đổi bằng `--save-dir <path>` nếu cần trỏ tới sandbox khác.

## Tùy chọn chung (đặt trước tên lệnh)

- `--save-dir SAVE_DIR` — thư mục chứa `pokemon-{a,b}.bin` (mặc định: `<repo>/fs_/.crosspoint`)
- `--no-backup` — bỏ qua tạo bản `.bak`
- `--dry-run` — in ra sẽ đổi gì mà không ghi thật

## Các lệnh

### `dump`
In toàn bộ party, record, gym progress (badge/lịch sử đánh gym), số lượng item trong túi, pending event. Dùng để xem trạng thái hiện tại trước khi sửa gì đó, hoặc kiểm tra lại sau khi sửa.

```sh
python3 scripts/dev/edit_pokemon_save.py dump
```

### `queue-encounter`
Xếp sẵn 1 wild-Pokémon pending event để test luồng bắt Pokémon ngay, không cần đọc sách chờ ngưỡng encounter.

```sh
python3 scripts/dev/edit_pokemon_save.py queue-encounter --species pidgey --level 5
```

- `--species` — id số hoặc tên loài (vd `16` hoặc `pidgey`)
- `--level` — 1-100
- `--gender` (`female`/`genderless`/`male`) — mặc định tự chọn giá trị hợp lệ theo loài (từ chối nếu gender bạn chỉ định không khớp `gender_rate` của loài, vd Chansey không thể `male`)
- `--slot` — ô pending-event 0-2, mặc định chọn ô trống đầu tiên (lỗi rõ ràng nếu cả 3 ô đều đầy)

### `reset-battle-store`
Xóa file phụ lưu HP/PP/moveset/status trận đấu (`pokemon-battle-{a,b}.bin` + file đơn cũ nếu còn) — lần vào trận kế tiếp của mỗi Pokémon sẽ tự dựng lại full HP/PP, hết status.

```sh
python3 scripts/dev/edit_pokemon_save.py reset-battle-store
```

### `reset-gym-progress`
Zero hóa `battleProgress` trong state — xóa sạch lịch sử đánh gym **và** huy hiệu đã có.

```sh
python3 scripts/dev/edit_pokemon_save.py reset-gym-progress
```

### `set-bag-item`
Set thẳng số lượng 1 item trong túi đồ (tự nhận diện đá tiến hóa `itemCounts` u16 hay item thường `bagCounts` u8 theo đúng item được chọn).

```sh
python3 scripts/dev/edit_pokemon_save.py set-bag-item --item "poke-ball" --count 10
```

- `--item` — id số hoặc tên item
- `--count` — số lượng muốn set

### `set-record-xp`
Set thẳng `totalXp` của 1 Pokémon record (party hoặc PC) theo `record-id` (xem id qua `dump`).

```sh
python3 scripts/dev/edit_pokemon_save.py set-record-xp --record-id 1 --xp 5000
```

## Ví dụ khác trong `--help`

```sh
python3 scripts/dev/edit_pokemon_save.py --help
python3 scripts/dev/edit_pokemon_save.py <lệnh> --help
```

Mọi tên species/item đều đọc trực tiếp từ `scripts/data/pokemon-kanto-v2.csv` và `scripts/data/pokemon-items.csv` (không hardcode), nên khi 2 file CSV nguồn đổi thì tool tự cập nhật theo, không cần sửa code tool.
