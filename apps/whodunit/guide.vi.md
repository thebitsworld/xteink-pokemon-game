# Whodunit

## Mục tiêu

Một vụ án mạng dạng lưới suy luận, mỗi lần chơi là một vụ mới. Mỗi nghi phạm có đúng một hung khí và ở đúng một địa điểm (ở mức Hard còn có một động cơ), và không có hai nghi phạm nào trùng nhau. Hãy suy ra ai cầm gì, ở đâu, tìm ra hung thủ và buộc tội đúng người, đúng hung khí, đúng nơi.

Mỗi vụ có đúng một đáp án và giải được hoàn toàn bằng suy luận - không cần đoán.

## Các mức

- Easy: 3 nghi phạm, hung khí và địa điểm.
- Medium: 4 nghi phạm, hung khí và địa điểm.
- Hard: 4 nghi phạm, hung khí, địa điểm và động cơ.

## Ba thẻ

### Clues (manh mối)

Các manh mối đánh số, rồi manh mối án mạng in đậm ("The body was found beside a broken teacup." - xác được tìm thấy cạnh một chiếc tách vỡ), rồi Case file (hồ sơ vụ án). Manh mối có vài dạng:

- "Baron Cobb was in the library" (ở thư viện) hoặc "was not in the attic or the kitchen" (không ở gác mái hay bếp).
- "Whoever had the brick was in the greenhouse or the attic" (người cầm viên gạch ở một trong hai nơi).
- Manh mối đặc điểm: "Whoever was in the attic was tall" (cao) hoặc "had a weapon that was heavy" (hung khí nặng). Xem Case file để biết nghi phạm nào cao, hung khí nào nặng.

Case file ghi đặc điểm của từng nghi phạm (thuận tay trái/phải, cao/thấp, màu mắt), từng hung khí (nặng/nhẹ, bằng kim loại hay không) và từng địa điểm (trong nhà/ngoài trời, cùng một chi tiết như "next to a stopped clock"). Chi tiết trong manh mối án mạng cho biết nơi xảy ra án; ai ở đó là hung thủ.

### Grid (lưới)

Lưới hình bậc thang, mỗi khối ứng với một cặp loại. Mỗi ô xoay vòng: trống, dấu X (không), vòng tròn (có), trống. Đánh vòng tròn sẽ tự gạch X phần còn lại của hàng và cột đó trong khối. Cặp đang ở dưới con trỏ được ghi tên dưới lưới.

### Accuse (buộc tội)

Chọn nghi phạm, hung khí, địa điểm (và động cơ ở Hard), rồi bấm Accuse. Chỉ được buộc tội một lần: đúng hay sai thì vụ án cũng kết thúc và đáp án hiện ra. Đồng hồ chỉ chạy khi vụ án đang mở.

## Điều khiển

Nút bấm (X3/X4): ở hàng thẻ, Left/Right đổi thẻ, Confirm hoặc nút cạnh Down để vào thẻ. Trong thẻ:

- Clues: Down, Right hoặc Confirm sang trang sau; Up hoặc Left về trang trước (Up ở trang đầu thì quay lại hàng thẻ).
- Grid: các nút hướng di chuyển con trỏ; Confirm đánh dấu ô. Up ở hàng trên cùng thì quay lại hàng thẻ.
- Accuse: Up/Down chọn dòng, Left/Right đổi lựa chọn, Confirm ở dòng Accuse để buộc tội.

Back mở menu; dấu đã đánh và thời gian được giữ lại.

Cảm ứng (X4 Pro): chạm các thẻ (Menu là thẻ cuối). Ở Clues, chạm nửa phải sang trang sau, nửa trái về trang trước. Ở Grid, chạm một ô để đổi dấu. Ở Accuse, chạm nửa trái hoặc phải của một dòng để đổi, rồi chạm Accuse.

## Mẹo cho người mới

- Đánh hết các dấu "không" trước; hàng nào trong khối chỉ còn một ô trống thì ô đó là "có".
- Nối thông tin giữa các khối: Hazel cầm sợi dây, sợi dây ở gác mái, vậy Hazel ở gác mái.
- Giải khối địa điểm sớm: manh mối án mạng chỉ cần biết địa điểm.
