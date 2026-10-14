# Nonogram

## Mục tiêu
Tìm ra bức tranh ẩn bằng cách suy luận ô nào trong lưới được tô. Các con số bên cạnh mỗi hàng và phía trên mỗi cột cho biết các ô tô trong hàng (cột) đó được chia thành những nhóm như thế nào.

## Cách đọc gợi ý
- Mỗi số là một nhóm ô tô liền nhau, theo thứ tự từ trái sang phải (hàng) hoặc từ trên xuống dưới (cột).
- "3 1" nghĩa là: một nhóm 3 ô tô, rồi ít nhất một ô trống, rồi một nhóm 1 ô.
- "0" nghĩa là cả hàng (cột) đều trống.
- Khi một hàng hoặc cột đã xong, gợi ý của nó chuyển sang chữ thường (không đậm) và bị gạch ngang.

## Luật chơi
- Tô (Fill) ô mà bạn chắc chắn thuộc bức tranh. Nếu sai, ô đó bị khóa thành lỗi (ô xám có dấu X đậm) và bộ đếm Mistakes tăng lên. Vì thế ô đã tô luôn là ô đúng.
- Đánh dấu (Mark) một ô (dấu X mảnh) để tự nhắc rằng ô đó trống. Dấu chỉ là ghi chú: không bao giờ bị tính lỗi và có thể xóa đi.
- Câu đố được giải khi tìm ra mọi ô tô của bức tranh. Không cần đánh dấu các ô trống.
- Mỗi câu đố có đúng một lời giải và luôn giải được bằng cách suy luận từng hàng, không cần đoán.
- Đồng hồ và số lỗi hiện ở trên cùng. Đồng hồ tạm dừng khi mở menu.

## Các mức độ
Easy 5x5, Medium 8x8, Hard 10x10 và Expert 12x12. Mỗi mức ghi lại số câu đã giải và thời gian tốt nhất. Mỗi câu đố mới được tạo ngẫu nhiên. Câu đố chưa xong được giữ lại khi thoát; chọn Resume để chơi tiếp.

## Điều khiển (nút bấm X3/X4)
- Left/Right di chuyển con trỏ theo hàng; hai nút bên cạnh (Up/Down) chuyển hàng.
- Bấm Confirm để tô ô.
- Giữ Confirm khoảng nửa giây để đánh dấu ô trống (hoặc bỏ dấu).
- Back mở menu. Giải xong, Confirm để sang câu đố tiếp theo.

## Điều khiển (cảm ứng X4 Pro)
- Chạm vào ô để tô.
- Công tắc Fill/Mark phía trên lưới đổi tác dụng của một lần chạm. Bạn cũng có thể giữ ngón tay trên một ô để đánh dấu.
- Chạm Menu để mở menu. Giải xong, chạm để sang câu tiếp theo.

## Mẹo cho người mới
- Bắt đầu từ số lớn. Hàng 5 ô với gợi ý 5 thì tô kín cả hàng; hàng 10 ô với gợi ý 7 thì 4 ô ở giữa chắc chắn được tô, dù nhóm nằm ở đâu.
- Cộng các số của một hàng và thêm một ô trống giữa mỗi nhóm. Nếu tổng bằng độ dài hàng thì chỉ có một cách tô.
- Đánh dấu các ô chắc chắn trống. Chúng giúp suy luận các hàng tiếp theo dễ hơn.
- Luân phiên xét hàng và cột: mỗi ô mới tìm được lại là gợi ý cho hàng (cột) cắt ngang nó.
