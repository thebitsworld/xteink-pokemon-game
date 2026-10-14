# Minesweeper

## Mục tiêu
Mở hết các ô không có mìn. Đào trúng mìn là thua.

## Luật chơi
- Chọn kích cỡ: Easy 9x9 với 10 mìn, Medium 12x12 với 24, Hard 16x16 với 40.
- Lần đào đầu tiên luôn an toàn: ô đó và các ô quanh nó không có mìn, nên sẽ mở ra một vùng.
- Ô đã mở hiện số mìn nằm sát nó (tối đa 8 ô xung quanh, tính cả đường chéo). Ô trống nghĩa là không có mìn nào, và các ô quanh nó tự mở ra.
- Cắm cờ lên ô bạn nghĩ có mìn. "Mines left" là số mìn trừ số cờ. Ô đã cắm cờ thì không đào được.
- Đào vào một ô số đã có đủ cờ quanh nó sẽ mở hết các ô còn úp xung quanh. Nếu cắm cờ sai, bạn sẽ trúng mìn.
- Mở hết các ô an toàn là thắng. Đồng hồ chạy từ lần đào đầu; thời gian tốt nhất của mỗi cỡ được lưu lại.
- Khi thua, mọi quả mìn hiện ra, và cờ cắm sai bị gạch chéo.

## Điều khiển (nút bấm X3/X4)
- Left/Right di chuyển con trỏ sang ngang, Up/Down (hai nút bên cạnh) di chuyển lên xuống.
- Confirm đào ô đang có khung (hoặc mở quanh một ô số đã đủ cờ).
- Giữ Confirm để cắm hoặc gỡ cờ.
- Back mở menu (Resume, các cỡ bàn, Exit). Hết ván, Confirm để chơi ván mới.

## Điều khiển (cảm ứng X4 Pro)
- Chạm vào một ô để đào. Chạm giữ một ô để cắm cờ.
- Công tắc Dig/Flag phía trên bàn đổi việc chạm thành cắm cờ; chạm lần nữa để về Dig.
- Chạm Menu để mở menu. Hết ván, chạm vào bàn để chơi ván mới.

## Mẹo
- Ô số 1 chỉ chạm đúng một ô còn úp: ô đó chắc chắn là mìn.
- Nếu một ô số đã đủ cờ, các ô úp còn lại quanh nó đều an toàn.
- Bắt đầu từ những chỗ đã chắc chắn rồi lần theo mép vùng đã mở.
