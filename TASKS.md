# Việc tồn đọng — Pokémon module

Ghi lại để khỏi phải hỏi lại. Nguồn đầy đủ, chi tiết (file:line, cách fix đề
xuất) nằm trong các file audit:
- [docs/development/pokemon-gen1-audit-round2.md](docs/development/pokemon-gen1-audit-round2.md)
- [docs/development/pokemon-gen1-audit-round3.md](docs/development/pokemon-gen1-audit-round3.md)
- [docs/development/pokemon-gen1-audit-round4.md](docs/development/pokemon-gen1-audit-round4.md) (nội dung của nó đã được xử lý xong trong đợt này)

File này chỉ là **danh sách rút gọn** để chọn việc tiếp theo. Xoá/cập nhật dòng
nào đã xử lý xong (kèm version/commit khi merge).

---

## Trạng thái hiện tại (cập nhật 2026-09-15, `v0.21.0`)

**Đã xong toàn bộ** trong đợt này (bug 2.9, tất cả 8 mục hiệu năng 3.1–3.8, và
7/8 tính năng+design-call round 2/3 — commit `009f2345`, `66b2e7fc`,
`19d0a28b`/`ae5b2430`, merge `214bd9ca`/`d2db0720`/`adf28780`, changelog
`8fd4b57b`). 24/24 native test pass, `pio run -e pokemon-x3` build thành công
(Flash 94.2%, RAM 22.4%) trên `main` sau merge.

## 1. Bug — ĐÃ XONG

- [x] **2.9** — Mutual-KO tự gây ra giờ báo "Pokémon của bạn cũng ngất" + ép đổi
  Pokémon trước khi tiếp tục Gym/Elite Four/Champion.
- [ ] *(ghi chú rủi ro còn treo, không phải bug đang xảy ra)* fix bug 2.6
  (Box-full gate trước khi ném Ball) dùng 1 gate trùng lặp riêng thay vì sửa
  tận gốc thứ tự gọi hàm gốc — nếu sau này 2 công thức lệch nhau, lỗi cũ có
  thể quay lại. Cân nhắc dọn lại khi có dịp đụng tới `Screen::BattleBalls`.

## 2. Hiệu năng — ĐÃ XONG (tất cả 3.1–3.8)

- [x] **3.1** — `PokemonStore::readRecords()` mới, quét 1 lượt cho cả party
  thay vì 6 lần `readRecord()` riêng.
- [x] **3.2** — `healPartyOnRead()` phần đọc dùng chung `readRecords()`.
- [x] **3.3** — Sắp xếp PC Box giờ 1 lượt quét duy nhất (đếm + xếp vị trí theo
  loài), không re-scan theo từng loài nữa.
- [x] **3.4** — Buffer ghi IV/EV (`IvEvStoreWriteFileBytes`, 518 entry) tách
  riêng khỏi buffer decode file cũ (vẫn giữ 1024 cho tương thích ngược).
- [x] **3.5** — Write-verify IV/EV giờ so byte trực tiếp (`memcmp`), không
  decode lại state nữa.
- [x] **3.6** — `BatchedRecordReader` nhận `chunkRecordCapacity` tuỳ chỉnh;
  `readRecord()` (tra 1 id) dùng chunk nhỏ thay vì luôn cấp ~2KB.
- [x] **3.7** — Summary screen đọc thẳng `focusedRecord_`, không gọi lại
  `service_.readRecord()` mỗi frame.
- [x] **3.8** — `refreshSnapshot()` tìm trong `snapshot_.party[]` (RAM) trước
  khi fallback đọc SD.

## 3. Tính năng thiếu so với Gen 1 — ĐÃ XONG 1.1–1.7, CHƯA LÀM 1.8

- [x] 1.1 — Miễn nhiễm trạng thái theo hệ (Fire không bị Burn, Poison không bị
  Poison/Toxic, Ice không bị Freeze)
- [x] 1.2 — Thêm 3 chiêu multi-hit: Double Kick (24, luôn 2 hit), Spike Cannon
  (131, phân phối 2/3/4/5 ngẫu nhiên), Bonemerang (155, luôn 2 hit)
- [x] 1.3 — Secondary stat-drop: Acid/Bubble Beam/Aurora Beam/Constrict/Bubble
  10%, Psychic ~33% (quirk gốc Gen 1)
- [x] 1.4 — Razor Wind (13) giờ là chiêu 2 lượt (không có bất tử như Fly/Dig)
- [x] 1.5 — Hyper Beam không bắt recharge nếu đòn đó hạ gục địch
- [x] 1.6 — Jump Kick/Hi Jump Kick gây 1 HP crash damage khi miss
- [x] 1.7 — Teleport: wild battle = thoát chắc chắn (như Run), trainer/gym =
  thất bại ("But it failed!")
- [ ] **1.8 — CHƯA LÀM, theo yêu cầu user (2026-09-15): Repel hiện chưa có ý
  nghĩa rõ ràng trong gameplay của bản mod này, cần suy nghĩ thêm trước khi
  triển khai.** Không tự làm khi chưa có yêu cầu mới.

## 4. Design-call round 2 — ĐÃ QUYẾT ĐỊNH VÀ LÀM XONG 5/8, GIỮ NGUYÊN 3/8

Đã hỏi ý kiến user (2026-09-15) và triển khai xong:

- [x] Move priority — Quick Attack +1 (luôn đi trước), Counter −1 (luôn đi
  sau), là khoá sắp xếp lượt đánh chính, Speed chỉ phá hoà khi cùng priority
- [x] Toxic — damage tăng dần `n × maxHP/16` mỗi lượt (n không giới hạn),
  tách biệt khỏi Poison thường (vẫn 1/8 cố định)
- [x] Chạy trốn (RUN, chỉ áp dụng wild battle) — có thể thất bại theo công
  thức Speed-ratio thật của Gen 1 (+ bonus mỗi lần thử thất bại); Gym/Elite
  Four/Champion vẫn luôn không chạy được (giữ nguyên)
- [x] Haze — giờ reset đầy đủ: status/confusion/Reflect/Light Screen/Mist/
  Focus Energy của cả 2 bên, không chỉ stat stages
- [x] Speed tie — tung xu 50/50 thay vì luôn ưu tiên player

**User chọn GIỮ NGUYÊN** (không đổi, đã ghi chú rõ trong code để audit sau
không hỏi lại):

- [ ] ~~Status/Leech Seed damage 1/8 vs 1/16~~ — giữ 1/8
- [ ] ~~Freeze 20%/lượt tự khỏi vs vĩnh viễn~~ — giữ 20%/lượt
- [ ] ~~Counter phản mọi đòn vật lý vs chỉ Normal/Fighting~~ — giữ phản mọi đòn

**2 quirk "nghi vấn" — đã xác nhận đúng theo Gen 1 gốc, giữ nguyên + comment
giải thích** (không phải bug):

- [x] Substitute hấp thụ damage vẫn feed Counter/Bide/Rage — quirk thật của
  Gen 1, đã ghi chú trong code
- [x] Thức dậy khỏi Sleep không tốn lượt — đúng hành vi Gen 1 gốc, đã ghi chú

---

## Lưu ý cần review thêm (phát sinh trong quá trình triển khai, chưa ai xác nhận)

- **Công thức chạy trốn dùng thêm hệ số `+30×số lần đã thử thất bại`** (theo
  Bulbapedia, không chỉ công thức rút gọn ban đầu) — nên xác nhận đây đúng là
  ý muốn, không phải diễn giải sai.
- **Move priority triển khai bằng hàm `movePriority(moveId)` hand-authored**,
  không thêm field vào `MoveData` (vốn sinh từ CSV) — theo đúng convention có
  sẵn của file (bảng crit/recoil/stat-change cũng hand-authored), nhưng khác
  với đề bài gốc yêu cầu thêm field.

---

## Quy ước làm việc (nhắc lại, đã thống nhất từ trước)

- Không push lên `origin` nếu chưa được đồng ý rõ ràng trong phiên đó.
- Mỗi việc lớn: tạo branch riêng → code → chạy full test suite native
  (`ctest -R Pokemon`) → build firmware (`pio run -e pokemon-x3`) → thêm
  `CHANGELOG.md` + bump version `platformio.ini` → merge `--no-ff` vào `main`
  → xoá branch.
- Việc chỉ mang tính nghiên cứu/audit (không sửa code) thì không cần tạo
  branch/version bump, chỉ commit doc.
- Với design-call: mô tả phương án trước, hỏi ý kiến, rồi mới code.
