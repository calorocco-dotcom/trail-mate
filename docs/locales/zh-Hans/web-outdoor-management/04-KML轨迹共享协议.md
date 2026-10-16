# KML 轨迹与路线去中心化共享：协议草案

2026-10-12 内容列表窄屏补充：要素标题、扩展字段名称与折叠标题支持长文本任意位置换行，按原页面边框与间距区分要素。实际 Edge 用 500 字符无空格的 KML 要素名称验证 390 像素宽度无横向溢出，标题内容完整保留，分页/扩展字段/纯文本描述与既有共享轨迹流程回归通过。网页构建通过，没有固件编译或设备 RAM 改动。

2026-10-12 共享详情内容浏览补充：完整 KML 下载验证后，地图下方提供可展开的内容列表，显示要素名称、文件夹路径、XML 要素 ID、描述、时间、样式引用及 Data 扩展字段。要素与每个要素的扩展字段各按 25 条分页，切换页不产生网络请求。描述、显示名、值使用 React 文本节点，不执行 HTML、不加载外部资源。实际 Edge 测试用 26 要素验证 25+1 分页，检查路径/ID/扩展字段，并确认描述中的 img/onerror 原样显示、无 img 元素、无脚本执行；共享签名、下载、搜索、维护及移动布局回归通过。新增内容浏览文案覆盖 11 语言并通过覆盖检查。网页构建通过，无固件编译或设备 RAM 改动。此列表不宣称已渲染全部 KML 扩展或覆盖/网络链接等特殊元素。

## manifest v1 当前实现约束

签名索引 bbox 计算规则：坐标按 E7 量化（半值向正无穷舍入），经度统一到 [-180,180)，每点相对文件首个坐标展开到 [-180,180) 差值，再累计经纬范围，最后包装回 E7 bbox。保留 west>east 跨日期变更线；这是包含全部量化点的保守范围，不保证最小圆弧。网页预览可以继续显示最小圆弧范围，签名清单构建必须采用上述规则与设备重算一致。点数包括全部受支持几何坐标，Polygon 闭合坐标也计数；不得跨独立路段推断连接。当前只实现累加器，设备 XML token/几何识别仍待接入。

网页 manifest 编解码与作者签名验证已实现，尚未接入设备签名/Reticulum 发布。固定 14 字段，最长 4096 字节，CMP1 canonical 编码；名称最多128 UTF-8字节，描述最多2048，时间为 Unix 秒（u64 wire，语义限制 0–253402300799），统计采用整数米/秒，未知时长和爬升为 null，点数1–100000。KML 文件1–8 MiB，MIME 固定 application/vnd.google-earth.kml+xml，profile=1。bbox 可 west>east 表示跨日期变更线，南北须有序。

首版为 Active 且 previous=null；新版需父版本精确 hash、同对象 ID、revision+1、创建时间不变、更新时间不倒退。归档须保留父版本 file 四字段；当前实现拒绝 Archived→Active，恢复策略仍需另行制定。作者签名验证不代表目录确认、文件已下载验真或当前最新版本；缺父版本不宣称版本链已验证。黄金编码向量（含超过 u32 的时间）固定在 track-manifest.test.mjs，当前只有网页实现验证，C++/Python 互通仍待完成。

补充修订：完整轨迹页面、KML 内容矩阵及 metadata/assets 协议以 [08-轨迹完整页面与KML能力](08-轨迹完整页面与KML能力.md) 为补充。原“必要子集”不作为静默丢弃其他 KML 内容的依据。

## 1. 领域边界

Track 是实际记录轨迹；Route 是供查看和跟随的路线；共享条目可声明 kind=track/route。共享文件统一为 KML，设备内部原始记录格式允许保留。录制中的文件不直接发布，先生成已封闭、不可变快照。

当前 `track_recorder.cpp` 支持 GPX/CSV/Binary，Route 已有 KML 列表和存储。需增加流式转换、KML 导入验证和共享服务，不能把改文件扩展名当成转换。

## 2. 内容与身份

共享对象由不可变 KML blob、作者签名 manifest、目录索引构成。文件 SHA-256 绑定精确 UTF-8 字节；发布后不得重新格式化文件却保留旧哈希。

manifest v1 使用固定顺序 MessagePack 数组：

`[1, author_pub:bin64, nonce:bin16, kind:u8, revision:u32, previous_manifest_hash:bin32|null, state:u8, created_at:u64, updated_at:u64, title:str, description:str, bbox:[west_e7,south_e7,east_e7,north_e7], stats:[distance_m,duration_s|null,ascent_m|null,point_count], file:[sha256:bin32,bytes:u32,mime:str,profile:u16]]`

签名输入：固定域字节 `TrailMate.Track.Manifest.v1\0` 加原始 manifest 字节；签名用设备 Ed25519，signed_manifest=`[manifest:bin,signature:bin64]`。校验作者公钥长度和 identity 绑定，不把文件自带 author 字符串作为证明。

object_id=`SHA256("TrailMate.Track.ID.v1\0" || author_pub || nonce)`；manifest_hash=`SHA256("TrailMate.Track.Revision.v1\0" || manifest_bytes)`。域字节固定为 UTF-8，bin 无额外编码；发布前冻结黄金向量。revision 从 1 开始，首版 previous=null，更新精确引用父版本。

建议状态 Active=0、Archived=1；归档创建签名新 manifest，保留 file 哈希。归档不删除网络历史，也不撤回已下载副本。恢复是否允许新 Active 版本列为审阅项。

## 3. 独立 Reticulum 应用协议

建议应用命名 `trailmate.track`，发现 aspect `discovery`，业务交付走 LXMF custom type `trailmate.track.v1`；最终 destination/aspect 构造必须验证 Python/浏览器/固件互通。签名 announce 声明角色、协议版本和能力，不复用 Geocaching 的 custom type。

请求：`[version=1,kind=0,op:u8,request_id:bin16,response_budget:u32,body]`。

响应：`[version=1,kind=1,op,request_id,status:u16,body]`。禁止跨协议请求 ID 混配，响应绑定具体目录身份。

| op | 名称 | body / 结果 |
|---|---|---|
| 0 | CAPABILITIES | 版本、角色、文件预算、分页、保存及断点能力 |
| 1 | QUERY | bbox、kind、作者、状态、snapshot/cursor；返回摘要和 manifest hash |
| 2 | GET_MANIFEST | object_id、精确 manifest hash；返回签名清单与当前性声明 |
| 3 | FILE_STATUS | file hash、size；返回保存状态和可恢复上传偏移 |
| 4 | FILE_PUT_BEGIN | file hash、size；返回绑定发送身份的 upload token |
| 5 | FILE_PUT_CHUNK | token、offset、bytes；返回已持久化偏移 |
| 6 | FILE_PUT_FINISH | token；完整哈希检查后返回保存回执 |
| 7 | PUBLISH | signed manifest、expected current hash；验证文件存在及版本后返回接受回执 |
| 8 | GET_FILE_CHUNK | file hash、offset、length；返回精确范围字节 |
| 9 | SYNC | 节点间增量交换签名 manifest；blob 按需复制 |

先发布文件后发布 manifest。文件存在不能冒充条目已发布。接受回执绑定 request_id、object_id、manifest_hash、revision、state、file hash、保存策略；目录/存储节点以其自身身份签名。拒绝作者不匹配、父版本不匹配及同 revision 不同 hash。

当前目录回执编码为 `[receipt_raw:bin, signature64]`，签名域为 `TrailMate.Track.Receipt.v1\0` 加原始canonical CMP1字节。raw固定13字段：`[1, request_id16, object_id32, revision_hash32, revision_u32, state_u8, file_hash32, file_bytes_u32, directory_public64, lxmf_delivery16, directory_epoch16, retention_policy=1, accepted_unix_seconds]`。policy=1表示该节点保存对应文件与清单历史；客户端核对已选目录公钥、推导LXMF地址、epoch、请求及已验签清单绑定。网络PUBLISH响应body为`[signed_receipt,current_0_or_1]`，current是该次查询声明，不在离线回执中代表永久最新性。Python签名/验签与错误请求/坏签名测试通过；固件与网页回执验证及真实传输尚待连接。

## 4. 大文件传输与预算

不把整份 KML 装入 Geocaching 8192 字节业务体。应用分块与 Reticulum Resource 分工：应用块用于持久化断点和配额；Resource 承载单块可靠传输，避免自己重做 RNS 链路协议。

首期建议基准预算：KML 最大 8 MiB、100000 点；应用文件块最多 16 KiB，网络接口协商可降至 1 KiB；单节点单发送者一个活动上传，设备单个活动大文件任务。它们是待压测的提案，发布前固定能力值，LoRa 低速模式必须展示估算与用户主动开始，不自动后台分发大文件。

每块绑定 file hash、upload token 和 offset，token 绑定发送者及有效期。节点限制临时空间、并发和速率；未完成上传有回收时间。范围下载跨来源时要求同一文件哈希与长度，完成后全文件 SHA-256 验证。首期应用级重连恢复；跨设备重启恢复要求偏移和文件任务日志落盘。

不能凭浏览器声明的 point_count/长度申请无限资源；流式解析计数。传输缓冲固定大小，整份 KML 不进入 ESP 栈或一次性 RAM 数组。

## 5. KML 互操作配置

首期支持 KML 2.2 Document/Folder/Placemark、Point、LineString、MultiGeometry 的必要子集；coordinates 顺序固定 longitude,latitude,altitude，水平坐标为 WGS84。记录点时间可通过声明的 gx:Track 配置保存；不支持的扩展明确报告，不悄悄丢弃时间。

输出多段轨迹保留段边界，不连出虚假直线；缺少海拔/时间不填伪造值。XML 转义正文，禁止 DTD、外部实体、NetworkLink、脚本和远程资源自动加载，暂不接受 KMZ。允许输入 KML 但不会自动执行 HTML description。

归档包可下载原始 KML 和签名 manifest/回执；单独 KML 仍可供第三方地图工具使用，但离开 manifest 后不保证作者验证。设备安装路线先验完整哈希和 KML profile，再原子写入路线目录，不覆盖正在使用对象。

## 6. 去中心化与检索

目录、文件节点、客户端是可分离角色；允许多个运营者，首期允许目录同时保存文件。节点发现通过签名 announce 和可编辑 bootstrap 列表，网页不依赖唯一内置服务器。

索引摘要未取回 manifest 验签前标为目录声明。各目录返回独立 snapshot/cursor；合并以 object_id/manifest_hash 去重。有效签名分叉保留冲突，不按接收时间选赢家；新版无法取回父版本时标记链未验证。

SYNC 传播签名版本和归档，不以目录自己的签名替代作者签名。节点可拒绝保存、限额或失联；UI 展示哪些节点接受、哪些保存文件，不承诺全网永久可用。地理查询处理跨日期变更线的范围拆分。

## 7. 网页与串口职责

### TRACK_COMMIT 请求字段

设备本地任务准入响应固定为 `[operation_id16, object_id32, revision_hash32, identity_generation_u32, revision_u32, directory_delivery16, directory_epoch16, admitted=1]`。网页核对全部字段后仅标记 localAdmitted，不能将其当目录确认或公开发布。提交客户端已实现同操作重试与失联后未确认结果，目标必须明确声明已验证的 `trailmate.track.v1` 协议；尚未挂到页面或设备提交运行时。

提交请求采用 canonical CMP1 数组 `[operation_id16, object_id32, revision_hash32, expected_identity_generation_u32, revision_u32, directory_delivery16, directory_epoch16]`，最长133字节。所有二进制绑定字段非零，代次与版本号为非零u32。操作ID沿用已准备操作，目录与epoch必须来自已验证的目录能力。设备在准入前重新验证当前作者、持久签名头、独立KML及目录状态；请求解码不代表授权或任务已接受。独立C++解码器已实现，提交任务、网络回执和网页客户端尚待连接，操作83尚未声明。

### TRACK_PREPARE 请求字段

准备请求采用 canonical CMP1 数组 `[operation_id16, kml_upload_id16, manifest_upload_id16, expected_identity_generation_u32, expected_parent_hash32_or_nil]`。三个 ID 均为非零 16 字节，两个上传 ID 必须不同；身份代次非零。首版父字段为 nil，更新/归档提供非零 32 字节 revision 域哈希。设备据类型固定目录加载两份封闭上传，重新验证清单及文件；父字段是当前持久对象头的比较条件，不能仅以网页声明作为版本授权。操作 ID 绑定持久预留和重试，不作为网络对象 ID。请求不含私钥、路径或完整清单。

解码器、平台准备任务及操作 82 分发入口已连接。成功响应采用 `[operation_id16, ready_0_or_1, object_id32_or_nil, revision_hash32_or_nil, revision_u32]`：准备中为 `[operation_id16, 0, nil, nil, 0]`；只有签名文件、预留记录和对象头持久化成功后才返回 ready=1。Busy 状态或准备中响应需要使用同一操作 ID 和相同请求继续轮询；活动任务仅接受原会话与完整请求绑定，30 秒不活动释放暂态内存。网页必须核对操作 ID 及预先计算的对象 ID、版本哈希、版本号；ready 不代表网络发布成功。浏览器 `device-prepare.js` 已实现该校验与有界轮询，尚未接入页面操作；在端到端验证完成前不声明该操作能力。

TRACK_LIST/TRACK_SNAPSHOT 获取设备记录；浏览器流式转换为 KML、预览并裁剪可选的敏感起终点，转换结果通过分块上传给设备。设备验证 manifest 与文件摘要及预算；TRACK_PREPARE/COMMIT 使用设备身份签名并建立网络任务。

设备不能盲信浏览器给出的距离、bbox 和文件哈希：哈希必须自行流式计算，字段范围验证；几何统计须流式重算或标记为作者声明，协议明确区分。首期将 bbox 与点数作为设备重算字段，距离/爬升方法与精度另冻结规范。

用户可以浏览、下载、导入设备、发布、更新、归档及查历史。修改几何会产生新的 KML 哈希和新版本；公开发布前显示位置公开范围。地图编辑复杂能力可分期，但发布/归档协议必须从第一期包含版本和回执。

## 8. 当前浏览器工作台实施

`site/tracks/` 已提供实际浏览器草稿工作台，从现有 Geocaching 与设备管理页的“轨迹”入口进入，复用 Animal Island 导航、卡片及背景。当前列表是本浏览器的未发布草稿，不代表设备所有记录或网络共享索引。可导入 KML、查看多段地图/统计、编辑文件名称与要素名称/说明/显隐、编辑独立坐标序列、地图添加航点、删除要素、撤销、保存/下载和删除浏览器草稿。

原文未修改时下载精确原始字节；编辑只在解析后的副本操作，验证后生成新字节。文件保留时间、样式定义/引用、文件夹、扩展与未绘制内容；外部资源不自动加载。绘制目前使用工作台的默认/选中样式，完整 KML 样式与 assets 显示仍待补齐，不能把原文保留等同于全部视觉能力完成。gx:Track 坐标编辑保留点数及高度，原 when/angles 等不丢弃；Polygon/环与其他内容保持解析/保留能力。

浏览器统计按每条独立 LineString/gx:Track 估算水平球面距离，不连接路段间隙；缺少绝对高度或完整时间时不填造累计上升/时间跨度。点坐标范围处理日期线，地图对不同路段采用共同经度窗口，避免把相邻日期线路段拉到世界两端。统计尚不能作为设备验证过的发布清单数据。

草稿按 IndexedDB 元数据与文件分开保存，最多 64 条、合计 64 MiB、单文件仍限 8 MiB。读回检查长度与 SHA-256；修改按预期本地版本原子更新，其他页面已修改则拒绝覆盖。未应用字段输入也受切换/刷新保护，保存和下载会校验并包含当前输入。未完成修改保留在编辑区，不将失败当成功。

三个实际 Edge 场景通过：KML 原有解析、编辑/统计（含 100000 点）及工作台流程。工作台覆盖真实 IndexedDB 持久化、精确原文下载、字段/坐标修改、地图航点、刷新恢复、时间/样式/扩展保留、未保存保护、跨页冲突、损坏文件哈希拒绝、日期线多段预览及 390px 无溢出。三个页面构建、现有设备/Geocaching 浏览器回归及 diff 检查通过。本轮仅浏览器变化，固件 RAM 没有增加。

尚未接入设备 TRACK_LIST/SNAPSHOT、原始记录转换/设备安装、作者签名 manifest、Reticulum 目录/文件传输、发布/更新/公开归档、他人共享列表/详情和地图/行政区/ID 检索。本文前面的完整协议目标保持，不能将当前本地工作台作为去中心化轨迹功能已经交付。
# 2026-10-12 共享详情统计实现补充

共享轨迹下载并验证完整 KML 后，详情地图上方显示浏览器计算的坐标点数、独立线段数、水平距离、绝对高度累计爬升和记录时间跨度，沿用现有 11 语言文案。仅有签名元数据时不冒充已有 KML 几何统计；缺失绝对高度或时间时显示未知，不填零。实际 Edge 测试核对两点单段距离约 1.47 km、未知爬升/时间、11 语言切换及无新增串口请求，共享轨迹已有签名详情、文件下载、搜索、维护与移动布局回归通过。网页构建通过，无固件编译或设备 RAM 改动。这些统计是浏览器估算，不代替设备/目录几何验证。

# 个人草稿安装入口补充（2026-10-12）

旧面板查询更新修正：confirmRouteInstallation 现使用同一作者指纹 Web Locks 排他锁，在 95 查询前及响应后核对当前浏览器记录的 id/hash/bytes/generation/stage；不同记录时报 ROUTE_CHECKPOINT_STALE，不覆盖新任务，也不发送 95。作者改变时报 ROUTE_AUTHOR_CHANGED；成功确认更新当前作者代次。实际 Edge 完整流程通过：新个人任务完成后旧共享面板查询被拒绝，85/95 计数不增加；刷新仍恢复最新个人记录，并正常只读确认。网页构建通过，未修改或编译固件。作者证明仍可发送只读 1/2，旧记录拒绝并不意味着零串口命令。

多入口记录清除修正：个人/共享面板可能各持有同一设备不同时间的记录，清除现使用与安装相同的 Web Locks 作者指纹锁，并核对完整 expected 记录；不同任务或不同阶段时报 ROUTE_CHECKPOINT_STALE，不清除新记录。清除按钮同步父级 disabled。4 项记录测试通过；实际 Edge 测试个人保存编辑时旧面板清除禁用、完成新个人安装后旧共享面板清除被拒绝，刷新后仍恢复新个人记录，安装/查询计数不因清除增加。网页构建通过，未编译固件。查询确认对旧记录的并发更新仍需进一步核对。

完整个人入口验证：实际 Edge 模拟串口测试先完成共享安装，再编辑个人草稿名称，验证未保存时个人安装按钮禁用且不发命令；保存后安装恢复可用，上传字节逐字节等于修改 Document/name 后的个人 KML（非旧共享源），封存哈希校验通过。个人安装 85 仅新增一次，95 查询确认；刷新重连从新个人安装记录只读恢复，无重装。全流程总计两次 85（共享一次、个人一次）和四次 95，身份读取失败后不增加计数；既有 11 语言、窄屏与共享回归通过。本轮只有测试与文档修改，没有编译固件；真实 SD 路线加载未验收。

设备连接区新增所选个人草稿的路线安装面板，展示草稿名称，复用共享详情安装/查询/恢复协议；输入为当前已保存模型的 KML 字节，dirty 或待应用编辑时不提供安装源，避免安装未保存内容。设备无能力时沿用既有不支持提示。网页构建和实际 Edge 共享回归通过，新增断言验证选中已保存个人副本后安装按钮可用；共享安装、刷新恢复、11 语言和窄屏仍通过。个人入口的完整上传字节及未保存状态切换仍需单独验证，本轮未修改或编译固件。
