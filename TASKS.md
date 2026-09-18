# Việc tồn đọng — Pokémon module

Ghi lại để khỏi phải hỏi lại. Nguồn đầy đủ, chi tiết (file:line, cách fix đề
xuất) nằm trong các file audit:
- [docs/development/pokemon-gen1-audit-round2.md](docs/development/pokemon-gen1-audit-round2.md)
- [docs/development/pokemon-gen1-audit-round3.md](docs/development/pokemon-gen1-audit-round3.md)
- [docs/development/pokemon-gen1-audit-round4.md](docs/development/pokemon-gen1-audit-round4.md) (nội dung của nó đã được xử lý xong)
- [docs/development/pokemon-gen1-audit-round5.md](docs/development/pokemon-gen1-audit-round5.md) (re-verify round 4, tìm bug mới 3.1 — đã fix `v0.21.1`)
- [docs/development/pokemon-gen1-audit-round6.md](docs/development/pokemon-gen1-audit-round6.md) (7 bug mới — 5 đã fix `v0.21.2`, xem mục "Round 6" bên dưới)
- [docs/development/pokemon-gen1-audit-round7.md](docs/development/pokemon-gen1-audit-round7.md) (bug lớn 1.1 đã fix `v0.21.2`; tính năng pity-counter đã làm `v0.22.0`; lifetime-summary vẫn chưa làm)

File này chỉ là **danh sách rút gọn** để chọn việc tiếp theo. Xoá/cập nhật dòng
nào đã xử lý xong (kèm version/commit khi merge).

---

## VẤN ĐỀ CHƯA GIẢI QUYẾT: X3 thật hiển thị ảnh grayscale kém hơn X4 Pro (2026-09-18)

**Hiện trạng: vẫn CHƯA fix được, 7 lần sửa liên tiếp đều không cải thiện gì
trên thiết bị thật.** Đọc kỹ mục này trước khi bắt đầu điều tra lại, tránh lặp
lại các hướng đã loại trừ.

**Triệu chứng**: ảnh hiển thị qua `SlideshowActivity`/`BmpViewerActivity` trên
X3 thật trông mờ/thiếu chi tiết hơn rõ rệt so với **cùng 1 file ảnh** hiển thị
qua Sleep Cover (`SleepActivity`, chế độ Custom) — user xác nhận trực tiếp
bằng mắt trên máy thật, không phải do ảnh chụp/moiré.

**File test cụ thể**: `images/sleep/IMG_3096.BMP` — 528×792 (dọc), 8bpp,
palette đã dither sẵn về gần 4 mức xám gốc (`nativePalette=true`, không qua
Atkinson/Floyd-Steinberg dithering ở decode-time).

**Đã LOẠI TRỪ (đã sửa, verify bằng debug overlay/build, KHÔNG cải thiện)**:
1. Full-refresh mỗi ảnh thay vì định kỳ (`v0.25.2`).
2. `renderer.preconditionGrayscale()` sau full-refresh (`v0.25.3`).
3. Driver-level: thêm `Uc8279Driver::beginGrayscale()` override ép
   `requestResync()` cho Absolute mode — **đã revert**, vì hóa ra
   `Uc8279Driver` không phải driver thật của X3 sản xuất (xem mục quan trọng
   bên dưới).
4. Bỏ bước vẽ lại B/W thừa + `cleanupGrayscaleWithFrameBuffer()` sau
   `displayGrayBuffer()` (copy nhầm từ `BmpViewerActivity`, `SleepActivity`
   không có bước này) — có lý, đã sửa, nhưng không cải thiện.
5. `bitmap.setDitheredOutputSize()` để re-dither đúng kích thước đích thay vì
   scale sau khi dither — không áp dụng được cho ảnh test này vì
   `nativePalette=true` (không qua ditherer nào cả, hàm no-op).
6. **Orientation mismatch** (Slideshow không ép Portrait như Sleep) — **đã
   xác nhận bằng debug overlay là fix đúng về mặt kỹ thuật**
   (`page=528x792 bmp=528x792 xy=0,0`, khớp 1:1 tuyệt đối, không scale/crop
   gì cả) nhưng **user xác nhận trực tiếp trên máy vẫn thấy khác biệt rõ
   rệt** — nghĩa là vấn đề KHÔNG chỉ là scale/orientation, còn gì đó trong
   pipeline grayscale.
7. `displayGrayscaleBase(HalDisplay::FAST_REFRESH)` → `HALF_REFRESH` trong
   nhánh Overlay (không phải Absolute) — có lý luận chặt chẽ (xem mục dưới)
   nhưng **vẫn không cải thiện**.

**Phát hiện quan trọng, GIỮ NGUYÊN (đã xác nhận đúng bằng debug overlay in
lên màn hình thật, không phải suy đoán)**:
- `renderer.supportsAbsoluteGrayscale()` trả về **FALSE** trên X3 sản xuất
  thật (`abs=0`). Toàn bộ nhánh `displayAbsoluteGrayscaleBase()`/Absolute
  mode KHÔNG BAO GIỜ chạy trên thiết bị này — mọi phân tích/sửa dựa trên
  `Uc8279Driver` (driver hỗ trợ Absolute) đều SAI MỤC TIÊU.
- Driver thật của X3 sản xuất là **`Uc8253X3Driver`**
  (`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8253X3Driver.cpp`,
  comment tự ghi "the production X3 implementation CrossPoint ships"),
  không phải `Uc8279Driver` ("newer production run"). Chỉ hỗ trợ
  `GrayscaleMode::Overlay`, không hỗ trợ `Absolute`.
- Đã trace kỹ luồng gọi thật:
  `GfxRenderer::displayGrayscaleBase(fallback)` →
  `HalDisplay::displayGrayscaleBase(fallback,...)` (gọi `requestResync(1)`
  nếu `fallback != FAST_REFRESH`) →
  `FreeInkDisplay::displayGrayscaleBase(fallback,...)` (2-arg, Overlay-only)
  → `_driver->beginGrayscale(bus, fb, Overlay, fallback, turnOff)` (KHÔNG
  override trong `Uc8253X3Driver`, dùng default của `PanelDriver` → gọi
  thẳng `displayGrayscaleBase(bus, fb, fallback, turnOff)`) →
  `Uc8253X3Driver::displayGrayscaleBase()` check
  `cleanBaseNeeded = !_redRamSynced || _grayState.lsbValid ||
  _forceFullSyncNext || _initialFullSyncsRemaining > 0`.
- `Uc8253X3Driver::requestResync()` **có** set `_forceFullSyncNext = true`
  (khác gì so với `Uc8279Driver`, đã verify code). Về lý thuyết, việc đổi
  `FAST_REFRESH` → `HALF_REFRESH` (mục 7 ở trên) PHẢI kích hoạt
  `cleanBaseNeeded=true` đúng như Sleep Cover — nhưng thực tế vẫn không đổi
  gì. **Chưa tìm ra tại sao lý luận này không khớp thực tế** — có thể còn 1
  bước nào đó reset `_forceFullSyncNext` giữa chừng, hoặc `cleanBaseNeeded`
  không phải là biến số quyết định chất lượng ảnh cuối cùng như đã giả định.

**2 hướng còn lại, CHƯA THỬ**:
1. **Debug qua serial/USB log thật** thay vì đoán qua ảnh chụp — thêm
   `LOG_DBG` ở tầng driver (`Uc8253X3Driver::displayGrayscaleBase()`) in ra
   giá trị `_redRamSynced`/`_grayState.lsbValid`/`_forceFullSyncNext` ngay
   tại thời điểm check `cleanBaseNeeded`, so sánh trực tiếp giữa lúc gọi từ
   Sleep Cover và lúc gọi từ Slideshow trên CÙNG 1 thiết bị — cần user có
   khả năng xem log qua cổng serial (chưa xác nhận có hay không).
2. **Bỏ hẳn grayscale, dùng B/W dither thuần cho Slideshow** — mirror đúng
   cơ chế **Contrast filter** của Sleep Cover
   (`SLEEP_SCREEN_COVER_FILTER::BLACK_AND_WHITE`/`INVERTED_BLACK_AND_WHITE`,
   `SleepActivity.cpp:705-717`) — filter này set `hasGreyscale=false`, bỏ
   HOÀN TOÀN pipeline grayscale (kể cả `cleanBaseNeeded` logic đang nghi
   vấn), chỉ `displayBuffer(HALF_REFRESH)` ảnh B/W thường. User đã xác nhận
   ban đầu filter này "hiển thị khá tốt" ở Sleep Cover. Đánh đổi: mất 4 mức
   xám, ảnh sẽ "gắt" hơn kiểu B/W thay vì mượt như grayscale thật — nhưng là
   hướng PRAGMATIC, không cần suy luận thêm về pipeline vì né được hoàn
   toàn, chưa triển khai.

**Trạng thái code hiện tại (đã commit)**: giữ nguyên tất cả các fix mục 1,
2, 4, 5, 6, 7 ở trên (không hại gì, có thể vẫn hữu ích một phần, và fix #6
orientation là cải tiến đúng đắn dù chưa giải quyết hết vấn đề chính) + đã
sửa comment trong code để không còn khẳng định sai "đây là fix đúng" ở
những chỗ đã chứng minh không đúng.

---

## Ý tưởng cải tiến/tính năng mới (2026-09-18) — CHƯA LÀM, đang cân nhắc

Từ một đợt rà soát toàn bộ tính năng hiện có so với các bản Pokémon gốc (agent
nghiên cứu, không sửa code), xếp theo độ phù hợp với giới hạn của bản mod này
(không multiplayer/wifi, flash hạn chế, core loop dựa thời gian đọc sách). Giữ
lại đây để tham khảo khi có thời gian, **chưa quyết định làm cái nào**.

1. **Màn hình tổng kết thành tích kiểu "Trainer Card"** — trùng với mục 2.2
   round 7 bên dưới (lifetime reading minutes, % Pokédex, số badge...). Dữ
   liệu đã có sẵn hết trong save, chỉ cần 1 màn hình hiển thị, không cần đổi
   save format. Rẻ nhất, "shovel-ready" nhất trong 4 ý tưởng.
2. **Thưởng/ghi nhận khi hoàn thành Pokédex 150/151** — hiện tại phần thưởng
   duy nhất khi bắt đủ 150 loài (chưa tính Mew) là *mở khóa Mew làm encounter
   hoang dã* (`mewIsReady()`, `lib/Pokemon/PokemonGame.cpp:239-245`), không
   có banner/item/danh hiệu nào khác đánh dấu thành tích này. Có thể gộp
   thành 1 banner trong màn hình ở mục 1 thay vì làm hệ thống riêng.
3. **Hall of Fame** — hiện khi thắng Champion (Blue) lần đầu, chụp lại đội
   hình 6 Pokémon lúc đó (sprite, tên/nickname, level, giới tính, shiny).
   2 phương án đã thảo luận với user:
   - **Phương án rẻ (đúng bản gốc)**: chỉ hiện 1 lần duy nhất lúc thắng, không
     lưu lại xem sau — chỉ cần 1 bit cờ mới (`hallOfFameCleared` hay tương
     tự) trong `PokemonState`.
   - **Phương án lưu lại xem sau**: cần thêm 1 side-file nhỏ snapshot 6
     record lúc thắng (để đội hình không bị "trôi" nếu sau này đổi/thả bớt
     Pokémon trong đội) — tốn thêm flash nhưng có giá trị lưu niệm hơn.
   User chưa chốt chọn phương án nào — hỏi lại khi bắt đầu làm.
4. **Vitamin tăng EV trực tiếp** (HP Up/Protein/Iron/Calcium/Carbos) — hiện
   EV chỉ tăng qua thắng battle (`awardBattleXp`'s EV yield), chưa có item
   nào tăng EV trực tiếp. Kỹ thuật rẻ (giống hệt pattern PP Up đã có: item id
   mới + công thức cộng có giới hạn + gắn vào drop pool Medicine có sẵn),
   nhưng làm loãng triết lý "EV chỉ tăng gián tiếp qua đọc sách → battle" nếu
   không cẩn thận định vị lại (ví dụ: vẫn chỉ rơi ra từ track đọc sách, không
   mua được). Độ ưu tiên thấp hơn 3 mục trên.

---

## Round 7 (2026-09-15) — bug 1.1 ĐÃ FIX (`v0.21.2`), tính năng pity-counter ĐÃ LÀM (`v0.22.0`)

- [x] **1.1 — ĐÃ FIX (`v0.21.2`)**: `useEvolutionItem()` (`lib/Pokemon/PokemonGame.cpp`)
  bị chặn bởi BẤT KỲ pending event nào, không chỉ event liên quan đến chính
  Pokémon đang evolve. Fix: scope lại đúng theo `record.recordId`, giống
  `resolveEvolution()`.
- [x] **2.1 — ĐÃ LÀM (`v0.22.0`)**: Thêm khối "Coming Up" dưới grid ở
  `Screen::Menu`, hiển thị 5 thanh progress bar pity-counter (Encounter/
  Balls/Medicine/TM-HM/Evolution) — căn giữa màn hình, cách nhau 1 dòng
  trắng, tự bỏ qua nếu không đủ chỗ. Không làm phần lifetimeMinutes/tier
  tiến độ sách (thấy dòng đó gây khó hiểu khi thảo luận, đã bỏ khỏi thiết
  kế cuối).
- [ ] *(chưa làm)* 2.2 — Màn hình tổng kết thành tích (lifetime minutes, %
  Pokédex, số huy hiệu...).

## Round 6 (2026-09-15, tại `v0.21.1`) — 5/7 bug ĐÃ FIX (`v0.21.2`), 2 mục giữ nguyên theo quyết định user

- [ ] **2.1 — GIỮ NGUYÊN (quyết định của user)** — Tỷ lệ tự đánh trúng mình
  khi Confusion vẫn dùng 33% (`CONFUSION_SELF_HIT_CHANCE_PERCENT`,
  `PokemonBattle.cpp:13`), dù Gen 1 thật là 50% — đã hỏi, user chọn giữ
  nguyên (nhẹ nhàng hơn cho người chơi). Đã thêm comment trong code xác nhận
  đây là lựa chọn có chủ đích.
- [ ] **2.2 — GIỮ NGUYÊN (quyết định của user)** — Paralysis vẫn giảm Speed
  còn 1/2 (`PokemonBattle.cpp:358`), dù Gen 1 thật giảm còn 1/4 — đã hỏi,
  user chọn giữ nguyên. Đã thêm comment trong code xác nhận.
- [x] **2.3 — ĐÃ FIX (`v0.21.2`)** — Substitute giờ chặn đúng flinch/secondary
  stat-drop/status chính trên đòn phá vỡ nó, dùng chung 1 snapshot pre-hit
  (`defenderHadSubstituteAtStart`) ở đầu `resolveGenericMoveEffect()`.
- [x] **2.4 — ĐÃ FIX (`v0.21.2`)** — Trap giờ giải phóng nạn nhân ngay khi bên
  gài trap ngất (`faintCombatant()` nhận thêm tham số `other`) hoặc bị đổi ra
  (`setupBattlePlayer()`/`setupBattleOpponent()` check trước khi reset).
- [x] **2.5 — ĐÃ FIX (`v0.21.2`)** — Reflect/Light Screen/Mist giờ sống sót
  qua switch trong cùng trận (tham số `preserveSideEffects`, true ở 3 điểm
  tiếp diễn cùng trận: đổi Pokémon tự nguyện, AI đổi, gym team tiếp theo;
  false ở các điểm bắt đầu trận mới). Guard Spec./Dire Hit/Focus Energy vẫn
  đúng per-Pokémon như cũ, không đổi.
- [x] **2.6 — ĐÃ FIX (`v0.21.2`)** — Đổi tên Pokémon đã sở hữu giờ quay lại
  đúng `Screen::Actions` thay vì luôn về Menu (`openNickname()` thêm tham số
  `routeSuccessThroughPendingEvent`, mặc định `true` giữ nguyên hành vi
  starter/catch, Rename truyền `false`).
- [x] **2.7 — ĐÃ FIX (`v0.21.2`)** — Ném Ball thất bại giờ gọi
  `finishItemUseMidBattle()` (dùng lại y hệt cơ chế item/switch), cho đối thủ
  ra đòn đúng luật.
- [ ] *(độ tin cậy thấp hơn, chưa quyết định, chưa fix)* 2.8 — Rage không
  tính damage từ Bide release hoặc tự đánh do Confusion.
- [ ] *(độ tin cậy thấp hơn, chưa quyết định, chưa fix)* 2.9 — PP Up áp dụng +
  lưu trước khi xác nhận tiêu item thành công — cùng dạng bug round 3 đã fix
  cho battle-boost item nhưng chưa áp dụng cho đường PP Up.

## Round 5 (2026-09-15, `v0.21.1`)

Audit vòng 5 re-verify toàn bộ 18 mục "đã xong" của round 4 (tất cả đúng, không
regression) và trả lời 2 câu hỏi mở còn treo (công thức run-away `+30×số lần
thử` — đúng theo Gen 1 thật; `movePriority()` hand-authored — đúng lựa chọn,
khớp convention có sẵn). Phát hiện 1 bug mới:

- [x] **3.1 (round 5) — ĐÃ FIX**: `toxicCounter` (field mới của Toxic tăng
  dần, round 4) không được lưu vào `BattleRecordEntry` — mỗi lần đổi Pokémon
  giữa trận, damage Toxic âm thầm tụt về 1/8 cố định. Fix: bump format
  `pokemon-battle-{a,b}.bin` lên v3 (21 byte/entry, theo đúng tiền lệ PP Up
  v1→v2), thêm `toxicCounter` vào 3 điểm đọc/ghi
  (`savePlayerBattleEntry()`, `setupBattlePlayer()`, item-sync mid-battle) +
  2 điểm cure-status trong `PokemonService.cpp` (đảm bảo invariant
  `toxicCounter` chỉ khác 0 khi `status == Poison`).

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
