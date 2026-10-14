# Patience

Hai trò xếp bài một người trong một ứng dụng: FreeCell và Nhện (Spider). Cả hai đều thắng nhờ tính toán chứ không nhờ may mắn.

## FreeCell: mục tiêu

Đưa cả 52 lá lên bốn ô đích, mỗi chất xếp từ át lên đến già. Các ván được đánh số như chương trình FreeCell quen thuộc, nên ván số 1 ở đây cũng là ván số 1 ở đó. Gần như ván nào cũng có thể thắng.

## FreeCell: luật chơi

- Bài được chia ngửa thành tám cột. Chỉ lá cuối của mỗi cột, lá hiện ra trọn vẹn, được di chuyển.
- Một lá đặt lên lá lớn hơn một bậc và khác màu: 6 đỏ lên 7 đen.
- Mỗi ô trống (free cell) giữ được một lá. Cột trống nhận bất kỳ lá nào.
- Có thể chuyển cả một dãy đã xếp đúng thứ tự cùng lúc, miễn là các ô trống và cột trống đủ để chuyển từng lá một: tối đa là số ô trống cộng một, nhân đôi cho mỗi cột trống. Dòng trạng thái ghi giới hạn này là "Max".
- Những lá không còn cần trên bàn sẽ tự lên ô đích.

## Nhện: mục tiêu

Nhện dùng hai bộ bài (104 lá). Xếp các dãy đủ từ già xuống át cùng một chất; dãy nào xếp xong sẽ rời khỏi bàn. Dọn đủ tám dãy là thắng. Chọn một chất (dễ), hai chất hoặc bốn chất (khó).

## Nhện: luật chơi

- Mười cột được chia, mỗi cột chỉ ngửa lá trên cùng. Lá úp sẽ tự lật khi lộ ra.
- Lá nào cũng đặt được lên lá lớn hơn một bậc, không kể chất. Chỉ dãy cùng chất mới chuyển được cùng lúc.
- Cột trống nhận bất kỳ lá hay dãy nào.
- Nọc chia thêm một lá lên mỗi cột, tổng cộng năm lần. Như luật cổ điển, không được chia khi còn cột trống.

## Điều khiển

Trên X3 và X4:

1. Trái và Phải đưa khung chọn qua các cột, rồi tới các ô trống và ô đích (FreeCell) hoặc nọc bài (Nhện).
2. Lên để lấy thêm lá trong dãy, Xuống để lấy bớt.
3. Xác nhận để nhấc bài lên (các lá chuyển màu tối), đưa khung tới chỗ muốn đặt rồi Xác nhận lần nữa. Xác nhận ngay tại chỗ vừa nhấc sẽ gửi bài tới chỗ hợp nhất: ô đích, lá vừa khớp, cột trống, rồi mới tới ô trống.
4. Quay lại để đặt bài xuống, hoặc mở menu.

Trên X4 Pro, chạm vào một lá để nhấc nó cùng các lá phía trên, rồi chạm vào chỗ muốn đặt. Chạm lại vào bài đang nhấc để gửi tới chỗ hợp nhất. Nút Undo và Menu ở góc trên bên phải. Lần chạm đầu không đi nước nào, nên lỡ tay cũng không sao.

Hoàn tác không giới hạn: có thể lùi tới tận lúc chia bài, từ menu hoặc nút Undo. Ván đang chơi được giữ lại khi thoát ứng dụng. Menu ghi số ván thắng, số ván đã chơi và chuỗi thắng dài nhất của từng trò; bắt đầu ván mới khi chưa xong ván cũ sẽ làm đứt chuỗi thắng.

## Mẹo

- Trong FreeCell, giữ các ô trống càng lâu càng tốt; một cột trống còn quý hơn vì nó nhân đôi số lá chuyển được.
- Giải phóng át và hai sớm: lá nhỏ bị vùi sâu là lý do thường gặp khiến ván bị kẹt.
- Trong Nhện, cố xếp lên lá cùng chất, vì chỉ dãy cùng chất mới chuyển cùng lúc được.
- Trước khi chia thêm từ nọc, hãy sắp xếp lại và lấp các cột trống: bài mới sẽ phủ lên tất cả.
