# Dungeon Map

## Mục tiêu

Câu đố logic. Vẽ tường cho bản đồ hầm ngục sao cho đúng mọi luật dưới đây. Mỗi bản đồ có đúng một lời giải, và bản đồ tự hoàn thành ngay khi tường của bạn đúng.

## Luật chơi

- Con số bên cạnh mỗi hàng và trên mỗi cột là số ô tường trong hàng/cột đó.
- Mỗi ngõ cụt - ô trống chỉ có một ô trống kề bên - có một con quái vật, và mỗi con quái vật đứng trong một ngõ cụt.
- Mỗi rương báu nằm trong một phòng kho báu 3x3: chín ô trống, đúng một rương đó, và đúng một lối ra.
- Ngoài phòng kho báu, lối đi chỉ rộng một ô: không có khối 2x2 nào toàn ô trống.
- Mọi ô trống phải nối liền thành một hầm ngục.

Quái vật và rương được vẽ sẵn, ô của chúng luôn là ô trống. Ô kề là trên, dưới, trái, phải (không tính đường chéo).

## Các mức

- Easy: 6x6.
- Medium: 8x8.
- Hard: 8x8 có một phòng kho báu.

Đồng hồ cho biết bạn đã giải bao lâu; menu lưu thời gian tốt nhất của mỗi mức.

## Đánh dấu ô

Mỗi ô là tường (tô đen), trống (chấm nhỏ) hoặc chưa biết (để trắng). Dấu chấm chỉ để bạn tự ghi nhớ: bản đồ chỉ được kiểm tra theo tường, ô trắng tính là ô trống. Số của hàng hay cột in đậm cho tới khi hàng/cột đó có đúng số tường, rồi chuyển sang chữ thường.

## Điều khiển

Nút bấm (X3/X4): Left/Right và nút cạnh Up/Down di chuyển con trỏ (đi quá mép thì vòng sang phía bên kia). Confirm đổi ô theo vòng: tường, trống, chưa biết. Back mở menu; bản đồ đang giải được giữ lại. Khi đã giải xong, Confirm tạo bản đồ mới.

Cảm ứng (X4 Pro): công tắc Wall/Open ở trên cùng chọn loại dấu mỗi lần chạm. Chạm một ô để đánh dấu, chạm lại để xoá. Menu ở góc trên bên phải. Khi đã giải xong, chạm vào bản đồ để chơi bản mới.

## Mẹo cho người mới

- Ô quái vật có đúng một ô trống kề: nếu ba phía đã là tường hoặc mép bản đồ thì phía thứ tư là ô trống, và ngược lại.
- Hàng có số 0 thì toàn ô trống; số bằng kích thước bản đồ thì toàn tường.
- Ô trống nào không có quái vật thì cần ít nhất hai ô trống kề.
- Tìm các khối 2x2 đã có ba ô trống: ô thứ tư phải là tường.
- Ở mức Hard, tìm phòng kho báu trước: phòng 3x3 quanh rương có tường bao quanh, chỉ chừa một lối.
