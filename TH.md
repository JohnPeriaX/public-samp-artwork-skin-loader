# คู่มือภาษาไทย

## โปรเจกต์นี้ทำอะไร

โปรเจกต์นี้เป็น ASI แบบ 32-bit สำหรับ GTA San Andreas และ SA-MP 0.3.7 บน Windows จุดประสงค์คือทำให้ client รุ่นที่เรารองรับสามารถใช้ custom artwork จาก open.mp ได้ โดยตอนนี้เลือกเก็บเฉพาะ **R3-1** และ **R5-1**

เหตุผลที่แยกตามรุ่นชัดเจน เพราะ SA-MP รุ่นเก่ามีโครงสร้างฟังก์ชัน ตำแหน่ง hook และรูปแบบข้อมูลที่ไม่เหมือนกัน การเอา address ของรุ่นหนึ่งไปใช้กับอีกรุ่นหนึ่งไม่ใช่วิธีที่ปลอดภัย ดังนั้นโปรแกรมจะตรวจ client ที่โหลดอยู่ก่อน แล้วค่อยติดตั้ง hook ของรุ่นนั้น

## R3-1 กับ R5-1 ทำงานคนละแบบ

### R3-1

R3-1 ใช้ทางที่โปรเจกต์นี้ทำขึ้นเองสำหรับ 0.3DL artwork โดยตรง ตัว plugin รับ artwork RPC, เก็บรายการ custom model, ขอไฟล์ DFF/TXD, ตรวจ CRC32 แล้วนำไฟล์เข้า cache

ตอน server ส่งข้อมูล skin แบบ 0.3DL จะมีข้อมูล custom skin เพิ่มเข้ามาจาก packet แบบเก่าของ SA-MP ด้วย `src/net/dlcompat.cpp` จะจำ custom ID ไว้ แล้วปรับ packet ให้กลับมาอยู่ในรูปแบบที่ handler ของ R3-1 เข้าใจได้

จากนั้น `src/game/render.cpp` จะนำ RenderWare clump ของ custom skin ไปแทนที่ clump ของ base skin ที่กำลังถูกใช้งานอยู่ ทำให้ตัวละครที่เกมสร้างตามระบบปกติยังใช้ base model เดิม แต่ภาพที่เห็นเป็น custom model

### R3-1: งาน custom object ที่กำลังทำ

ตอนนี้ล็อกงานไว้ที่ R3-1 และกำลังต่อยอดจาก custom skin ไปยัง 0.3.DL **simple object** ด้วย `AddSimpleModel` ซึ่งใช้ custom object ID ช่วง `-1000` ถึง `-30000` และนำ ID ไปใช้กับ `CreateObject` หรือ `CreatePlayerObject`

ฝั่ง R3 ตอนนี้ตรวจ IDA แล้วพบว่า client มี 0.3.DL model manager และ native object pipeline อยู่แล้ว โดย simple object ใช้ custom ID ช่วง `-1000` ถึง `-30000` และถูกส่งต่อผ่าน `CObjectPool::Create` ได้โดยตรง งาน source จึงเปลี่ยนมาเปิดใช้ native pipeline นี้และวางไฟล์ DFF/TXD ลง native cache แทนการเอา negative ID ไป index `CModelInfo` ของ GTA งานส่วนนี้ **ยังไม่ถือเป็น runtime PASS** จนกว่าจะทดสอบ `AddSimpleModel` + `CreateObject` จริง

### R5-1

R5-1 มีของดีอยู่ใน `samp.dll` มากกว่าที่เห็นจากภายนอก ตัว client มี native 0.3DL artwork manager และทาง render object อยู่แล้ว ส่วนที่ขาดคือ download worker ที่ทำงานจริง

`src/net/nativeart.cpp` จึงไม่สร้าง artwork manager ใหม่ทั้งหมด แต่เปิดทางให้ handler ของ R5 ทำงาน แล้วเติมส่วน download/cache ที่หายไปให้ เมื่อไฟล์ DFF/TXD ถูกดาวน์โหลดและตรวจ CRC แล้ว native manager ของ R5 ก็สามารถมองเห็นไฟล์ตาม cache format ที่มันคาดไว้

สำหรับ custom skin ยังใช้ clump swap ของโปรเจกต์นี้ เพราะการที่ native manager มี model ไม่ได้แปลว่า player ped จะเปลี่ยนไปใช้ custom clump เองทั้งหมด

## ทำไมโปรเจกต์นี้จึงโฟกัสที่ R3-1 และ R5-1

โปรเจกต์นี้ตั้งใจโฟกัสเฉพาะ client สองรุ่นที่สามารถแยก code path และอธิบายพฤติกรรมจากหลักฐานที่มีได้ชัดเจน

R3-1 มี direct 0.3DL bridge และผ่านการทดสอบ skin แบบเห็นผลจริง ส่วน R5-1 ใช้ native artwork manager ที่มีอยู่ใน client แล้วเติมเฉพาะ download/cache path ที่ขาด การแยกสองเส้นทางนี้ช่วยให้ revision detection, packet adaptation, artwork loading และ RenderWare integration ตรวจสอบได้ง่ายขึ้น

โค้ดหรือการทดลองที่ไม่ใช่เส้นทางรองรับปัจจุบันจะไม่ถูกเก็บไว้ใน public implementation เพื่อไม่ให้เกิด stale hook, mapping หรือ build path ที่คนอื่นอาจเข้าใจผิดว่าเป็น feature ที่รองรับ

ดังนั้น public repository จึงระบุ R3-1 และ R5-1 เท่านั้น และแยกสถานะ runtime ของแต่ละรุ่นตามหลักฐานจริง ไม่อนุมานจากการที่ source สามารถ build ได้เพียงอย่างเดียว

## โครงสร้าง source

### `src/core`

ส่วนนี้ดูแล logging และการตรวจว่า `samp.dll` เป็น client รุ่นไหน

หลักการตรวจ revision อยู่ประมาณนี้:

```cpp
if (g_imageSize == r5::kImageSize && g_timeDateStamp == r5::kTimeDateStamp)
    g_revision = Revision::R5_1;
else if (g_entryPointRva == r3_1::kEntryPoint)
    g_revision = Revision::R3_1;
```

ข้อดีคือ hook ไม่ได้เริ่มทำงานเพียงเพราะไฟล์ชื่อเหมือนกัน โปรแกรมต้องเห็น fingerprint ของ binary ที่รู้จักก่อน

### `src/net/masquerade.cpp`

ส่วนนี้ดูแลขั้นตอน connection ที่ทำให้ server เปิดทาง artwork ให้ client ที่รองรับ โดยแยกเรื่อง protocol identity ออกจากเรื่อง parsing packet

การแยกแบบนี้ช่วยเวลาหา bug เพราะเราจะรู้ว่า server ไม่ส่ง artwork หรือ client รับ artwork แล้วแต่ parse ผิด เป็นคนละปัญหากัน

### `src/net/dlcompat.cpp`

นี่คือจุดที่เอา 0.3DL skin packet มาปรับให้เข้ากับ SA-MP รุ่นเดิม

ข้อมูลที่สำคัญของ R3-1 มีลักษณะประมาณนี้:

```cpp
const uint16_t playerId = *reinterpret_cast<uint16_t*>(d + 0);
const uint32_t skin = *reinterpret_cast<uint32_t*>(d + 2);
const uint32_t customSkin = *reinterpret_cast<uint32_t*>(d + 6);

*reinterpret_cast<uint32_t*>(d + 0) = playerId;
*reinterpret_cast<uint32_t*>(d + 4) = skin;
p->numberOfBitsOfData = 64;
```

ก่อนเรียก handler เดิม เราจะจำ custom ID เอาไว้ แล้วเอา packet กลับไปเป็น layout ที่ handler R3-1 รู้จัก

แนวคิดเดียวกันถูกใช้กับ WorldPlayerAdd และ SetSpawnInfo โดยเอา field custom skin ที่ 0.3DL เพิ่มเข้ามาออก แต่ต้องตรวจขนาด packet ก่อนทุกครั้ง

### `src/net/directdl.cpp`

นี่คือ artwork client ของ R3-1 ที่อยู่ใน plugin เอง

มันเก็บข้อมูล custom model เช่น base ID, custom ID, DFF/TXD CRC, ขนาดไฟล์ และ virtual world จาก artwork RPC แล้วดูว่าไฟล์ไหนยังไม่มีใน cache

ลำดับหลักคือ:

1. รับ model manifest
2. ตรวจ cache
3. ขอหรือดาวน์โหลด DFF/TXD
4. ตรวจ CRC32
5. เขียนลง `.part`
6. เปลี่ยนชื่อเป็นไฟล์ cache จริง
7. ส่งสถานะให้ artwork state machine ไปต่อ

การใช้ `.part` มีความสำคัญ เพราะ native loader ไม่ควรเห็นไฟล์ที่กำลังเขียนอยู่

### `src/net/nativeart.cpp`

ส่วนนี้เป็นหัวใจของ R5-1

R5 มี native artwork manager อยู่แล้ว ดังนั้นเราไม่สร้างระบบ object renderer ใหม่ แต่ใช้ handler ของ native manager แล้วเติม download worker ให้มัน

ภาพรวมคือ:

```text
ModelUrl RPC
    |
    v
nativeart.cpp
    |
    v
model/download.cpp
    |
    v
ตรวจ CRC32
    |
    v
<CRC>.part
    |
    v
<CRC>.dff / <CRC>.txd
    |
    v
R5 native artwork manager
```

วิธีนี้ลดจำนวนส่วนที่ต้อง reverse-engineer ใหม่ เพราะสิ่งที่ R5 ทำได้อยู่แล้วก็ปล่อยให้ R5 ทำต่อ

### `src/model`

`download.cpp` ทำ HTTP download แบบแยก thread ออกจาก game thread

worker รับงานจาก queue, ดาวน์โหลดไฟล์, ตรวจ CRC แล้วส่งผลกลับมาให้ main thread เป็นผู้ commit cache และเปลี่ยน state ของ artwork

ส่วนหนึ่งที่สำคัญคือ user agent:

```cpp
InternetOpenA("SAMP/0.3", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
```

server artwork ต้องการ user agent แบบนี้ จึงไม่ใช่แค่เปิด URL แล้วถือว่าจบ

### `src/game/render.cpp`

ส่วนนี้คือขั้นตอนที่ทำให้ custom skin "เห็นจริง" ในเกม

ระบบจะหา model-info ของ base skin แล้วจำ clump เดิมของเกมไว้ จากนั้นเมื่อ custom skin ถูก assign และ artwork โหลดพร้อมแล้ว จึงเปลี่ยน field ให้ชี้ไปยัง custom clump

เมื่อ custom skin ไม่ได้ใช้งานแล้ว จะ restore clump เดิมกลับมา

สำหรับงาน R3 รอบปัจจุบัน object ใช้ native 0.3.DL pipeline ของ R3 แล้ว โดย plugin รับ RPC 179/183/185 เข้าสู่ native model manager, ดาวน์โหลด DFF/TXD ลง native cache และอ่าน entry `type=2` เพื่อ mirror metadata ส่วน `CModelInfo::AddClumpModel` ไม่ถูกใช้กับ negative SA-MP object ID งานนี้ **ยังอยู่ระหว่างทำ** จนกว่าจะผ่าน live test ด้วย `AddSimpleModel` + `CreateObject` จริง

ยังมีการตรวจกรณี streamer ของเกม unload clump ออกไปแล้วด้วย เพราะ pointer เก่าที่เคยเก็บไว้ไม่ควรถูกนำกลับไปเขียนทับ model ที่เพิ่งโหลดใหม่

R3-1 ยังมี hook ใน spawn path เพื่อเปลี่ยน custom model ID กลับเป็น base model ID ก่อนที่ SA-MP จะสร้าง ped

### `src/net/raknet.cpp` และ `src/net/artwork.cpp`

สองส่วนนี้รวม low-level network access และโครงสร้าง artwork ที่ใช้ร่วมกัน ช่วยให้แต่ละ revision ไม่ต้องสร้างระบบ connection ของตัวเองใหม่

### `vendor`

เก็บ dependency ขนาดเล็กที่ใช้ในงานนี้ เช่น RakNet interface และ urmem สำหรับ hook

## การทดสอบที่มีหลักฐานจริง

## ผลการ build ล่าสุดของ PC

source tree ปัจจุบันถูก build ใหม่ด้วย Visual Studio 2022 Community MSBuild สำเร็จแล้ว:

- Configuration: `Release | Win32`
- MSBuild: `17.14.60`
- MSVC: `14.44.35207`
- ผลลัพธ์: **0 warnings, 0 errors**
- ไฟล์ output: `build/bin/Release/custommodels.asi`
- SHA-256: `A65E526E58DED5432D9B4CFCDF37C559CF457A925329C6E4C2C59108885041D0`

ผลนี้ยืนยันเฉพาะ **source/build path** เท่านั้น ยังไม่เลื่อนสถานะ R3 custom-object เป็น runtime PASS และไม่ได้ประกาศ R5 runtime PASS

## R3-1 Secure Asset Storage

รอบนี้เพิ่ม protected asset path สำหรับ R3-1 โดยเป้าหมายคือไม่ทิ้ง DFF/TXD แบบ plaintext ไว้ใน cache ถาวรบนเครื่องลูกค้า

- downloader รับไฟล์เข้า RAM แทนการเขียน DFF/TXD ลง disk โดยตรง
- ตรวจ CRC32 ก่อนเก็บ asset
- asset ที่เก็บถาวรถูกเข้ารหัสด้วย AES-256-GCM พร้อม nonce ใหม่ต่อ asset และ authentication tag
- key AES ถูกสร้างด้วย `BCryptGenRandom` และเก็บเป็น DPAPI-protected blob แบบผูกกับ user profile ของ Windows
- ก่อนให้ `samp.dll`/RenderWare ใช้งาน จะ decrypt ลง native cache เป็น staging file ชั่วคราว
- หลัง RenderWare โหลด clump/TXD ครบแล้ว plugin ลบ staging plaintext ออกจาก disk
- หาก streamer ต้องโหลด clump ใหม่ plugin สามารถ materialize จาก protected cache กลับมาได้
- ถ้า protected asset ถูกแก้ไขหรือ decrypt ไม่ผ่าน จะไม่ถือว่าเป็น asset ที่ใช้งานได้

ข้อจำกัดที่ต้องระบุให้ชัด: R3 ตอนนี้ยังรับ asset ผ่าน HTTP ตาม flow เดิมของ open.mp ดังนั้น encryption รอบนี้ป้องกัน **persistent client cache / การ copy cache ไปใช้ต่อ** เป็นหลัก ยังไม่ใช่ end-to-end encrypted transport. การทำ server-side encrypted asset + session key/TLS จะเป็นขั้นถัดไปก่อนนำระบบนี้ไปใช้กับ asset เชิงพาณิชย์จริง

หลักการ AES-GCM/nonce uniqueness, CNG และการไม่ hardcode key สอดคล้องกับแนวทางของ Microsoft สำหรับ authenticated encryption และ key protection. DPAPI โดยค่าเริ่มต้นผูกข้อมูลกับ user credential และเครื่องที่ใช้ปกป้องข้อมูล

### Build evidence รอบ Secure R3

- Configuration: `Release | Win32`
- MSBuild: `17.14.60`
- MSVC: `14.44.35207`
- Result: **0 warnings, 0 errors**
- Output: `build/bin/Release/custommodels.asi`
- SHA-256: `A65E526E58DED5432D9B4CFCDF37C559CF457A925329C6E4C2C59108885041D0`
- `git diff --check`: exit 0; มีเฉพาะคำเตือนเรื่อง LF/CRLF
- Runtime after GUI-fix build: **PASS**
- Runtime evidence: เปิด GTA ใหม่แล้ว R3-1 โหลด artwork ผ่านตามปกติ และ GUI เดินจาก `Updating models.. [0/6]` ต่อจนโหลดครบ
- Deployment: `C:\Games\Grand Theft Auto San Andreas\custommodels.asi` ถูกแทนด้วย build นี้แล้ว และ SHA-256 ตรงกัน

## R3-1 Asset Download GUI: root cause และ fix รอบ 2026-10-18

ภาพ `sa-mp-002.png` แสดง native dialog ของ R3-1 ชัดเจนว่า UI ไม่ได้หาย แต่ค้างที่ `Updating models.. [0/6]` ขณะที่ protected downloader ดาวน์โหลดครบทั้ง 6 asset และสร้าง protected cache สำเร็จ

### Root cause ที่ยืนยันจาก IDA

R3-1 native client มี callback สำหรับ progress/completion ของ download UI อยู่จริง:

- GUI singleton pointer: `samp.dll + 0x26E8A0`
- native GUI updater: `samp.dll + 0x06A570` (`sub_1006A570`)
- start/reset dialog: `samp.dll + 0x06A100` (`sub_1006A100`)
- completion path ของ native file worker เรียก updater ด้วย `state=4` เมื่อไฟล์หนึ่งเสร็จ
- native worker ยังตั้ง readiness byte ของ artwork entry (`+0` DFF / `+1` TXD) ก่อน artwork manager ไปต่อ

ก่อนหน้านี้ plugin downloader ทำ HTTP/CRC/secure-cache เอง แต่ไม่ได้ส่ง completion callback ตัวนี้กลับเข้า native R3 จึงเกิดอาการ UI รู้ว่ามี 6 ไฟล์ แต่ไม่มี event ว่าไฟล์ใดเสร็จ ทำให้ค้าง `0/6` และ native `.temp` ที่ตัวเองสร้างไว้ยังคงอยู่

จุดนี้ **ไม่ใช่ bug ที่เพิ่งเกิดจาก securecache** ผู้ใช้ยืนยันว่า GUI มีปัญหามาตั้งแต่ก่อน securecache และ IDA ยืนยันว่า architecture เดิมของ downloader ก็ bypass native GUI completion path อยู่แล้ว

### สิ่งที่แก้ใน R3

- `model/download.cpp` เพิ่ม completion hook ที่ทำงานบน downloader worker thread ซึ่งเป็นบริบทเดียวกับ native download callback ที่ IDA พบ
- `directdl.cpp` รับ completion แล้ว:
  1. materialize plaintext ชั่วคราวเข้า native cache
  2. ตั้ง DFF/TXD ready flag ของ native R3 artwork entry
  3. เรียก `sub_1006A570` ด้วย `state=4` และจำนวน byte จริง เพื่ออัปเดต native GUI
  4. บันทึกไฟล์เป็น completed
  5. เมื่อครบ 6 ไฟล์ ส่ง `FinishDownload` RPC 184
- cache hit และ inline payload ใช้ completion path เดียวกัน
- `Pump()` เหลือเป็น fallback เพราะ native R3 download dialog สามารถ block heartbeat ของ `CNetGame::Process` ระหว่างรอ download
- R5 ไม่ถูกแตะใน logic นี้ และยังคงล็อกไว้

### Build / deploy evidence ของ GUI-fix รอบนี้

- `Release | Win32`
- MSBuild `17.14.60`
- MSVC `14.44.35207`
- **0 warnings, 0 errors**
- build SHA-256: `A65E526E58DED5432D9B4CFCDF37C559CF457A925329C6E4C2C59108885041D0`
- deployed SHA-256 ที่ `C:\Games\Grand Theft Auto San Andreas\custommodels.asi`: ตรงกัน 100%
- Runtime verification หลัง build นี้: **PASS**
- เปิด GTA ใหม่แล้ว R3-1 โหลด artwork ผ่านตามปกติ; GUI ไม่ค้างที่ `0/6` และ flow download เดินต่อจนจบ
- ผู้ใช้ยืนยันผล runtime ว่าโหลดผ่านปกติหลัง GUI-fix

## การทดสอบที่มีหลักฐานจริง

R3-1 ถูกทดสอบกับ local open.mp artwork harness โดยใช้ custom skin สองตัว:

- `20001` จาก base skin `1`
- `20002` จาก base skin `2`

คำสั่งคือ `/skin1`, `/skin2`, `/normal`

ผลที่เห็นจริงคือ `20001` -> `20002` -> CJ ปกติ และ client log สอดคล้องกับการเปลี่ยน skin แต่ละขั้น

ส่วน R5-1 ในเอกสารนี้ยังไม่เรียกว่า runtime PASS เพราะยังไม่มีหลักฐาน live test รอบใหม่ที่ควรใช้ยืนยันสถานะนั้น

## เครดิต

ขอขอบคุณ **Ricardo Fernandes (`ricardoofnl`)** และ repository [`customskin-0.3.7-R5`](https://github.com/ricardoofnl/customskin-0.3.7-R5) เป็นพิเศษ เพราะงาน R5 ของเขาช่วยให้เห็นภาพสำคัญว่า `samp.dll` ของ R5 มี native 0.3DL artwork manager อยู่แล้ว และส่วน download worker คือจุดที่ต้องเติมให้สมบูรณ์

เราใช้ repository นั้นเป็น reference สำหรับแนวทาง R5 แต่ไม่ได้อ้างว่า source ของโปรเจกต์นี้เป็นสำเนาของเขา ส่วน R3-1 direct bridge, payload adaptation, RenderWare integration, revision gates และ debugging history เป็นงานของ repository นี้

References อื่น ๆ:

- [openmultiplayer/open.mp](https://github.com/openmultiplayer/open.mp)
- [spmn/samp-client-compat](https://github.com/spmn/samp-client-compat)
- [spmn/urmem](https://github.com/spmn/urmem)
- [BlastHackNet/SAMP-API](https://github.com/BlastHackNet/SAMP-API)
- [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)
- [AmyrAhmady/sampvoice](https://github.com/AmyrAhmady/sampvoice)

## สรุปแนวทางของโปรเจกต์

เราเลือกแนวทางที่อธิบายได้มากกว่าทางที่ดูเหมือนทำงานแต่ยังตอบไม่ได้ว่า ABI และ model ownership ถูกต้องจริงหรือไม่

ดังนั้น public implementation จึงเก็บเฉพาะ R3-1 และ R5-1 โดยแยกสถานะ runtime ของแต่ละรุ่นตามหลักฐานที่มี
