---
title: Build & Flash lên X3
parent: Development
nav_order: 6
---

# Build firmware và nạp lên Xteink X3

Hướng dẫn từng bước để tự build firmware (bản có module Pokémon) và nạp lên thiết bị X3 thật.

## Trước khi bắt đầu

**X3 không có cổng USB truy cập dữ liệu** (theo [docs/user-guide.md](../user-guide.md), mục Troubleshooting: *"The X3 has no user-accessible USB data port"*). Nghĩa là:
- `pio run --target upload` (flash qua cáp USB, cách dùng cho board dev thông thường) **không dùng được** trên X3.
- Cách nạp firmware **duy nhất** là qua **thẻ SD**, kích hoạt từ menu Settings trên máy.

## Bước 1 — Chuẩn bị môi trường (chỉ cần làm 1 lần)

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive   # chỉ cần nếu freeink-sdk trống
```

Nếu PlatformIO chưa cài, hoặc `pio` không nằm trong PATH mặc định, đường dẫn thường là `~/.platformio/penv/bin/pio` (dùng `export PATH` như trên).

## Bước 2 — Build firmware bản có Pokémon

```sh
pio run -e pokemon-x3
```

Dùng đúng environment `pokemon-x3` (không phải `default`) — chỉ env này bật cờ `CROSSINK_ENABLE_POKEMON`.

Số liệu Flash/RAM in ra ở cuối log (`RAM:`, `Flash:` %). Nếu build fail vì vượt dung lượng partition, xem [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md) mục ràng buộc dung lượng.

## Bước 3 — Lấy file firmware

Sau khi build xong, script `rename_firmware.py` tự copy firmware ra tên dễ dùng, tại:

```
.pio/build/pokemon-x3/firmware-x3-x4.bin
```

## Bước 4 — Đưa file `.bin` vào thẻ SD của X3

Chọn 1 trong 2 cách:

### Cách A — Tháo thẻ SD, cắm vào máy tính (chắc chắn nhất)

1. Tắt nguồn X3, tháo thẻ SD, cắm vào máy tính.
2. Copy `firmware-x3-x4.bin` vào **gốc thẻ SD** (không tạo thư mục con).
3. **Không format thẻ, không xóa/đổi thư mục nào khác** — sách, tiến độ đọc, save Pokémon (`/.crosspoint/pokemon*.bin`) phải giữ nguyên.
4. Lắp thẻ lại vào X3.

### Cách B — Không cần tháo thẻ, dùng File Transfer qua Wi-Fi

1. Trên X3: mở **File Transfer**.
2. Máy tính/điện thoại cùng mạng Wi-Fi, mở địa chỉ IP mà X3 hiển thị lên trình duyệt.
3. Upload `firmware-x3-x4.bin` lên gốc thẻ SD qua giao diện web đó.
4. Thoát File Transfer trên X3.

## Bước 5 — Kích hoạt cập nhật trên thiết bị

Trên X3: **Settings → System → SD Card Firmware Update** → chọn file `firmware-x3-x4.bin` vừa copy → xác nhận.

## Lưu ý an toàn

- **Backup trước khi nạp lần đầu với build lạ**: sao lưu `/.crosspoint/pokemon-a.bin` và `pokemon-b.bin` trước, phòng trường hợp không mong muốn — dù các thay đổi save format trong nhánh này (xem [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md)) đã được thiết kế để tương thích ngược với save cũ.
- Nếu firmware crash, X3 tự lưu crash report vào gốc thẻ SD (không cần USB) — kiểm tra file log đó nếu có vấn đề sau khi nạp.
- Ở các giai đoạn (GĐ) chưa có UI dùng tới tính năng mới, nạp bản build sẽ **trông và chạy giống hệt** bản trước đó — chưa có gì để "thấy" khác biệt trên màn hình. Đây là bước xác nhận "firmware nạp được, không crash, save không hỏng", chưa phải bước "chơi thử tính năng mới". Xem roadmap để biết giai đoạn nào đã có UI.

## Tham khảo thêm

- [Getting Started](./getting-started.md) — cài đặt PlatformIO, build cho các environment khác
- [Pokémon Battle Roadmap](./pokemon-battle-roadmap.md) — tiến độ, ràng buộc kỹ thuật, việc cần làm tiếp
- [docs/installation.md](../installation.md) — quy trình cập nhật chính thức dành cho người dùng cuối (bản release đóng gói sẵn)
