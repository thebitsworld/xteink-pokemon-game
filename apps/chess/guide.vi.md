# Chess (Cờ vua)

## Mục tiêu
Chiếu hết Vua đối phương: tấn công Vua sao cho nó không còn đường thoát. Bên White (Trắng) đi trước. Ứng dụng áp dụng đầy đủ luật cờ vua.

## Các quân cờ
- Vua (King): đi một ô theo mọi hướng. Không bao giờ được đi vào ô bị chiếu.
- Hậu (Queen): đi thẳng hoặc chéo, bao xa cũng được.
- Xe (Rook): đi thẳng, bao xa cũng được.
- Tượng (Bishop): đi chéo, bao xa cũng được.
- Mã (Knight): đi hình chữ L (hai ô một hướng, rồi một ô sang ngang). Mã nhảy qua được các quân khác.
- Tốt (Pawn): tiến một ô, hoặc hai ô từ vị trí xuất phát. Tốt ăn chéo một ô về phía trước.

## Nước đi đặc biệt
- Nhập thành: Vua đi hai ô về phía một Xe, và Xe nhảy qua đứng cạnh Vua. Chỉ được khi cả Vua lẫn Xe chưa đi, các ô ở giữa trống, Vua không đang bị chiếu và không đi qua hay dừng ở ô bị tấn công. Nhấc Vua rồi chọn ô cách hai bước.
- Bắt tốt qua đường (en passant): Tốt vừa tiến hai ô có thể bị Tốt địch đứng cạnh ăn như thể nó chỉ tiến một ô.
- Phong cấp: Tốt đến hàng cuối sẽ thành Queen, Rook, Bishop hoặc Knight; ứng dụng sẽ hỏi bạn chọn quân nào.

## Kết thúc ván
- Chiếu hết là thắng. Không bao giờ được để Vua mình bị chiếu.
- Hòa khi: hết nước đi mà không bị chiếu (stalemate), mỗi bên đi 50 nước không ăn quân và không đi Tốt, cùng một thế cờ lặp lại ba lần, hoặc không đủ quân để chiếu hết (chỉ còn hai Vua, hoặc Vua và một Tượng hay một Mã đấu với Vua).

## Bắt đầu ván
- vs Device: chơi với máy ở mức Easy, Normal hoặc Hard. Nút "You play White/Black" đổi bên của bạn; cầm Đen thì bàn được xoay để quân bạn ở phía dưới.
- Two players: hai người chơi lần lượt trên cùng máy.
- Take back a move: đi lại nước vừa rồi (và cả nước đáp của máy); sau đó chọn Resume.
- Resume: chơi tiếp; ván được giữ khi thoát ứng dụng.

## Điều khiển (nút bấm X3/X4)
- Left/Right và hai nút bên cạnh (Up/Down) di chuyển con trỏ.
- Confirm nhấc quân (Pick); chấm và vòng tròn cho thấy ô có thể đến. Đưa con trỏ tới đó rồi Confirm (Move).
- Khi phong cấp, Left/Right chọn quân, Confirm để xác nhận.
- Back bỏ quân đang nhấc, hoặc mở menu. Hết ván, Confirm để chơi lại.

## Điều khiển (cảm ứng X4 Pro)
- Chạm quân của bạn, rồi chạm ô muốn đến. Khi phong cấp, chạm quân muốn chọn. Chạm Menu để mở menu.

## Mẹo cho người mới
- Dùng Tốt chiếm trung tâm, sớm đưa Mã và Tượng ra.
- Nhập thành sớm để Vua an toàn.
- Đừng đưa Hậu ra quá sớm: nó sẽ bị đuổi chạy khắp bàn.
- Trước mỗi nước, xem nước vừa rồi của đối thủ đang đe dọa gì.
- Đếm số quân tấn công và bảo vệ trước khi ăn quân.
