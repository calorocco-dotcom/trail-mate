# Geocaching 网页维护：设备身份与协议规划

## 1. 复用边界

复用 `modules/core_geocaching` 的记录格式、作者签名、Publish/Query/Get/Sync、版本策略及设备发布日志。网页新增维护入口，不修改当前 v1 wire 语义。当前网页位于 `site/geocaching`，设备维护集中在 `platform/esp/arduino_common/src/geocaching/browse_runtime.cpp`；新串口入口需要调用可复用的用例服务，不能模拟屏幕按键或复制一套发布状态机。

## 2. 作者与传输

作者固定为连接设备的 combined public key；设备执行签名。网页已有临时访客身份只用于浏览，不冒充设备作者。首期发布由设备现有 Reticulum/LXMF 通道执行，外层发送者和记录作者遵守当前目录验证规则。

浏览器 WSS 可继续用于浏览。网页通过 WSS 转交设备签名记录的混合模式暂不作为首期依赖，实施前必须确认外层 LXMF 身份绑定规则、发布回执验证和设备日志同步。

## 3. 串口维护流程

- CACHE_LIST_OWNED 使用设备作者公钥分页检索本地草稿和已知发布记录，返回来源与是否目录确认；不能承诺本地列表等于网络所有作者记录。
- CACHE_DRAFT_PUT 使用 draft_id 和 expected_generation 更新草稿，返回字段错误。记录体超出单帧时走公共对象传输。
- CACHE_PREPARE（67，已接入草稿准备）请求 `[draft_id16, draft_generation64, directory_delivery16, author_generation32, prepare_id16]`。prepare_id 由浏览器以密码学随机数生成，在 Busy/断线重试中保持不变；它是操作关联标识，不是作者凭证。设备读取已提交内容及现有本地历史，向目标目录检查当前版本，返回 `[prepare_id16, draft_id16, draft_generation64, author_public_key64, directory_delivery16, directory_epoch16, draft_content_hash32, from_revision32, to_revision32, remaining_ms32]`。内容哈希绑定完整已提交草稿，不代表已经生成或签名发布记录。
- CACHE_COMMIT（68，已接入）请求 `[prepare_id16, draft_content_hash32, author_generation32]`；prepare_id 同时作为发布 operation_id，重试不得另造 ID。设备再次校验原准备票、草稿哈希、目录 epoch、作者及版本；首次提交还排除操作 ID 与保留任务冲突，耗时读取后刷新身份、boot 和有效期。先持久化 Intent，再准入现有异步版本预留、签名、出站保存和传输流程。返回 `[operation_id16, draft_id16, draft_generation64, directory_delivery16, draft_content_hash32, to_revision32, local_admitted=1]`，只表示本地任务已准入，不表示目录接受。
- CACHE_HISTORY/JOB_GET 返回签名版本、目录回执和待处理任务；断线重连按 operation_id 查询，不能重新增加 revision。

PREPARE token 有效期建议 120 秒；绑定草稿 generation、作者 generation、基版本和内容哈希，任何变化使其失效。具体预算通过协商返回。

### 发布任务结果投影（JOB_GET 已接入，待真机验收）

JOB_GET（48）请求 `[operation_id16, author_generation32]`。设备核对当前作者身份代次，先查当前运行任务，未匹配时查询并验证 SD 历史；不会因查询恢复任务或发送请求。能力协商已返回操作 48/50/68，共 20 项。

网页 operation_id 同时为 Task ID，显式历史读取直接查 Task 索引，再读取其引用的出站请求；不扫描全量出站历史。请求 ID、任务 ID 和所属子请求必须一致，多个匹配目录作为歧义拒绝，引用丢失作为损坏处理。后台恢复没有指定操作 ID 时继续使用原扫描策略。两种读取都保留现有根/缓冲租约，不新增任务缓存。

当前运行时投影为 `[operation_id16, phase, cache_id32|null, revision_hash32|null, revision32, durable_confirmed, transport_confirmed, error|null]`。阶段编号：0 准备中、1 持久化中、2 等待目录、3 目录已确认、4 已停止、5 失败、6 停止中。两个 confirmed 字段均为 `0/1/nil`，nil 表示没有足够证据，网页保留为 null，不能显示成未发送。错误按 UTF-8 最多 128 字节校验，以文本展示。

签名对象尚未准入时 cache/hash 可以同时为空；生成后必须与网页保留的操作、对象及版本一致。等待阶段必须有签名对象且持久化已确认；目录已确认必须同时具有持久化和传输确认，且没有错误。停止和失败属于本地终态，不表示公开成功，也不证明目录没有接受。仅阶段 3 对应 directoryConfirmed。未知阶段、错配 ID、非法证据组合均拒绝。

浏览器解析及查询实现位于 `site/geocaching/src/device-publication-job.js`。Busy 只表示读取尚未完成，有限轮询后保留原操作 ID；Waiting 任务快照直接返回给界面。当前和历史均未找到保留结果时，设备返回 EXPIRED（10）；此操作的 EXPIRED 表示结果缺失，不证明从未提交或从未发布。客户端转成 JOB_UNKNOWN 并保留 ID，不能按通用“重新开始”提示再次发布。DeviceDraftReader.job 与其他维护操作共用串行读取资源，结束时等待 SESSION_CLOSE 确认，关闭读取不取消持久发布任务。

历史存储读取复用 PublicationRecoveryFilter 的显式只读模式，必须绑定 operation_id；可以读取停止或已撤销继续意图的请求，后台恢复模式继续排除它们。Task 状态与出站请求状态分别保留，因为取消只修改任务，不能由任务停止推断请求未发送。encodePublicationHistoryJob 与 readWebPublicationHistory 已实现持久请求作者签名、对象/版本哈希和保存回执关联校验；等待/停止/失败的未知传输证据保留 nil，受阻和本地保存失败具有失败原因。JOB_GET 不恢复 PublishAttempt、不写日志、不启动传输，也不能根据历史 NotFound 自动重复提交。可见确认/恢复入口已连接，真实 USB/RNS 与断电恢复验收仍未完成。

## 4. 网络版本与归档

JOB_CANCEL（50）已接入，请求与 JOB_GET 相同：`[operation_id16, author_generation32]`。它核对身份代次、操作 ID、保存作者和当前设备作者，调用 requestWebPublicationStop，再读取并返回同样八项任务投影；Busy 仅表示停止或存储尚未完成，网页保留原 ID。结果缺失为 EXPIRED，不允许自动重新发布。网页确认停止后才发送 50，使用原 ID 有限重试，并只接受阶段 3 或 4 的完成回执；暂停浏览器等待不撤销已发送的停止请求。

当前持久 PublishAttempt 复用原停止事务，由现有维护循环推进。没有匹配当前任务时，可按操作 ID 读取并验证已签名历史请求，在同一运行时锁内恢复后请求停止，不重新签名；其他活动任务不能被替换。冷启动只读快照先完成存储日志恢复，再创建可写事务。正在保存目录确认时等待完成，晚到停止不会覆盖已确认结果。Ready 可以表示本地已停止或目录已经确认，回执必须区分，不能直接显示“撤回成功”。已停止历史任务及已发送等待任务的冷启动停止、重复停止和无新增发送/签名回归已通过；签名前意图及受阻恢复仍需补齐，当前这些状态不能承诺主动停止。目录可能已收到请求，本地停止不是公开归档；后续查询若目录已确认，界面更新为确认结果。

记录已经签名但 PublishAttempt 尚未准入时，运行时保留停止请求；异步历史读取或首次持久准入后立即进入 cancel，不依赖网页持续重试。这个窗口的 JOB_GET 为阶段 6、签名对象标识暂为 nil、耐久和传输证据未知；只有停止事务完成才返回阶段 4。单次停止、随后只查询的原生回归验证无新增发送/签名。浏览器在发送 50 前保存 stopping 公开恢复记录，刷新后显示上次停止待确认，保留同 ID；普通等待投影不清除停止意图，用户可继续查询或再次停止，不提供恢复提交入口。该浏览器记录不是设备已持久停止的证据。

保持原 cache ID 派生及记录编码。新增版本引用 previous_hash，revision 按当前策略递增，时间不得倒退。归档生成原作者签名的 Archived 新版本；本地删除草稿与公开归档是两个命令。

目录接收确认必须校验原请求 ID、对象 ID、revision hash、revision、state 和目录签名/身份。链路 ACK、LXMF 送达证明都不能替代应用级接受回执。相同版本不同内容作为分叉冲突展示，不自动覆盖。

设备与网页共用发布历史。设备本地历史可能落后于网络，提交前必须对选定目录执行当前版本检查；多个目录不一致时显示来源分歧。离线允许编辑，未完成检查时不声称最新版本。身份更换后只能发布新身份内容，不能更新旧作者记录。

目前准备凭据使用单个 SD 槽：Checking（3）只表示等待网络，不能用于提交；网络不存在仅允许本地 from=0 的首版计划，现有版本必须同时匹配目录版本号及已提交草稿的基版本哈希。完成后转为 Prepared（0），从完成时间起有效至多 120 秒；同 ID 重试返回原确认信息并扣减剩余时间，不重新延长有效期。新提交核对当前作者/身份代次、boot、有效期和全部请求绑定。Intent（1）先于准入保存，Accepted（2）仅代表本地准入；同 ID 重试先读取当前/历史任务，已有任务不再次准入，已保存结果可在重启/过期后返回。新 ID 可替换过期/旧 boot 的 Checking/Prepared；同一会话、作者及身份代次也可替换尚未提交的 Checking/Prepared，支持返回编辑后重新检查。其他会话不能抢占尚有效的准备，未解决 Intent/Accepted 不可覆盖。Accepted 只有在验证已确认或持久停止终态后才能让出槽。

恢复缺口仍存在：旧 boot 的 Intent 没有出站任务，或 Accepted 找不到当前/历史证据时，提交返回 Busy 并保留原 ID，不重签或重发。相同 boot、尚在原有效期内的 Intent 在未找到任务且重新核对全部绑定后可重试准入。意图到出站保存之间断电、身份变化后的单槽处置、受阻任务的取消/恢复仍需补齐并实测；不能据目前测试宣称整个断电恢复闭环完成。

### 现有页面中的确认与恢复交互

“以设备身份发布”卡位于维护列表和目录选择区，原地图及右侧编辑抽屉保留。已保存、无未保存修改的草稿才能检查；重新读取设备快照后调用 67，确认对话框列出作者指纹、目标目录、版本变化、坐标、状态、难度/地形、容器、正文和提示。原生 dialog 提供焦点限制与 Esc 返回编辑，确认前不发送 68。

68 前同步保存公开关联记录到浏览器存储，按作者公钥隔离，包含原会话 owner、操作 ID、草稿代次、目标、内容哈希及版本。白名单不包含私钥、配置或正文。浏览器刷新后的单调有效期不复用；未提交准备必须重新检查，待核查提交保留原 ID，重连后恢复原 owner 并再次证明设备作者。存储损坏不删除，首次记录写入失败不继续新提交；缓存状态标记“浏览器上次记录”，不假装是新设备查询。

任务卡区分结果未知、等待目录、已确认、停止和失败；查询等待只释放读取，不取消持久任务。停止原任务有单独确认入口，取消确认不发送 50；完成后显示本地停止及目录可能已收到的说明。确认结果后刷新设备列表并关闭对应已保存编辑器，其他草稿未保存编辑保留；公开 cache ID 可复制。尚未确认任务禁止替换设备草稿或创建替代发布，受阻任务主动停止及意图断电窗口的恢复仍待实现。

浏览器 commitDeviceDraft 验证准备结果与已证明作者，确认前拒绝过期，Busy 重试保留同一 ID，回执逐字段匹配；应答途中超出准备有效期不否定真实准入，需 JOB_GET 查询。显式 resume 只允许重用原 ID，由设备判定是否存在原任务/意图，不创建新操作。DeviceDraftReader.commit 复用串行资源并确认 SESSION_CLOSE；关闭只释放读取。提交绑定查找复用四行目录窗口，切换窗口/会话后可重新定位目标，不分配全量目录缓存。

当前目录查询复用 Reticulum Get（operation=3），请求体 `[cache_id, nil, nil]`。只有目标目录与请求 ID 匹配、回复 `200` 标记当前且无冲突、记录 ID/作者签名验证通过，才使用其版本/哈希。已有目录服务的 `404` 精确错误体 `["not_found", nil, nil]` 表示该目录没有该记录；设备以独立 `Absent` 结果返回，不与未知目录、超时或 `404/revision_unavailable` 混同。`409` 分歧、`410` 归档策略及其他错误同样不能作为新建许可。这个结论仅限选定目录，不能宣称全网不存在；发布提交仍需处理检查与提交之间的并发冲突。

## 5. 交互与验收

页面提供“浏览”“我的缓存”“草稿”“发布任务”；地图录入坐标，正文按实际 UTF-8 字节预算校验，预览完整记录。当前记录限制 4096 字节、应用负载 8192 字节，名称/正文/提示分别受现有模型约束。

发布前显示作者指纹、内容摘要、新旧版本与目标目录。提交后展示本地保存、网络发送和目录确认三个进度事实。USB 断线后提示任务可能继续，重连恢复结果。

验收：原作者发布/更新/归档、错误作者拒绝、重复 COMMIT 单次签名版本、并发版本冲突、目录拒绝、超时后收到迟到回执、掉电恢复、端侧和网页交替维护、既有浏览/GPX 下载回归。网络协议使用现有向量验证，不能以本地表单成功替代发布验收。

## 6. 已接入的串口读取格式

`CACHE_DIRECTORY_LIST (70)` 请求 `[offset:u32]`，最多读取设备既有四槽发现缓存，offset=4 仅可返回结束标记。响应 `[offset, known_count, row|null]`，行是 `[delivery:bin16, discovery:bin16, public_key:bin64, epoch:bin16, sequence:u64, name, flags:u8, max_query_items:u8, age_ms:u64]`；flags 位 0 表示能力已协商，位 1 表示有在途查询。目录来自既有原生通告认证入口，浏览器再次核对公钥派生的 delivery/discovery 地址；这不是全网目录总数，也不证明此刻可达。该读取激活已有网络查询运行时，但不请求 SD 仓库初始化。

以下为当前实现，前文其余维护命令仍是待完成契约。

- `CACHE_LIST_OWNED (64)` 请求 `[offset:u32, expected_catalog_generation:u64|null, author_generation:u32]`。首次 offset 为 0 且允许空目录代次；后续必须携带首次取得的目录代次。
- 响应 `[catalog_generation:u64, offset:u32, known_count, row|null]`。known_count 为已有索引提供的已知数量下界，不是承诺完整总数。空 row 是此位置无条目的结束标记。
- 每次返回一行：`[draft_id:bin16, draft_generation:u64, author:bin64|null, name, state, latitude_e7|null, longitude_e7|null, base_hash:bin32|null, publication]`。publication 为 `[latest_revision, confirmed_revision, flags]`，flags 的位 0/1/2/3 分别为 pending/stopped/local_changes/base_retained。
- 本地目录包含当前设备保存的草稿，包括历史身份草稿。浏览器按作者公钥区分可维护与只读，不能把这个接口当作网络作者记录全集或目录确认。
- `CACHE_DRAFT_GET (65)` 请求 `[draft_id:bin16, offset:u32, count:1..320, expected_draft_generation:u64, author_generation:u32]`；首次草稿代次可为 0，继续分块必须携带实际代次。响应 `[draft_id, draft_generation, total_bytes, offset, chunk]`。
- 两个读取接口均核对作者代次；目录或草稿代次变化返回 Conflict，作者变化返回 StaleIdentity，后台尚未完成返回 Busy。`SESSION_CLOSE (3)` 使用空数组请求/响应，释放网页 Geocaching 占用，不关闭 USB，也不取消已持久化发布任务。

设备共用原有四行目录窗口，每次直接编码一行到命令缓冲。网页应串行读取，结束、取消或失败时释放维护占用；不得无界请求所有页面，也不得把已知数量展示成精确总数。

草稿上传暂存复用公共 TRANSFER_BEGIN/CHUNK/STATUS/FINISH/ABORT（32–36）。BEGIN 的 kind=1 是配置，kind=2 是编码后的草稿编辑体，草稿暂存上限 4096 字节，块仍最多 320 字节。两类共用一个上传槽、相同 owner/transfer_id/哈希/耐久偏移校验及 111 字节 SD 日志，不能并行上传。kind=2 的 FINISH 只证明收到完整内容且哈希一致，不证明草稿语义、作者权限或保存完成。kind=2 不能通过 CONFIG_VALIDATE/COMMIT 操作写入配置。

`CACHE_DRAFT_PUT (66)` 已接入，请求 `[transfer_id:bin16, draft_id:bin16, expected_generation:u64, author_generation:u32]`，预期代次不能是 uint64 最大值。响应 `[transfer_id, draft_id, saved_generation]`，保存代次为 expected+1。服务端先检查实际设备作者代次与上传 owner，再将传输绑定到草稿、预期代次及作者代次；绑定后不能换目标。存储权限从当前已存草稿判断，不接受上传体自称作者。

草稿日志 phase 为 0 接收、1 哈希封存、2 目标绑定、3 已持久化提交意图、4 已持久化成功回执。phase 3 写入后才排队后台候选加载和编辑事务。Busy 表示尚未确认保存，客户端有界轮询相同请求；内存中的完成标记不能单独构成成功。服务端通过已存草稿 expected+1 代次及规范编辑哈希确认结果，再写 phase 4 并返回回执；之后相同请求可读取耐久回执而不重复保存。若暂存文件已缺失，已存草稿仍可证明同一编辑完成。

phase 0/1/2 的未提交对象允许 ABORT；phase 3/4 拒绝直接取消和覆盖。已证明未进入存储事务的后台失败可退回 phase 2 后取消，不能把未知存储失败当作未提交。提交后、回执持久化前又发生后续编辑，或作者更换后需要查询旧操作等更复杂历史恢复尚待补齐；真实 SD 掉电验收未完成，不把当前回执路径称为所有故障场景完成。

## 7. 已确认内容的历史恢复（后续源码，尚未刷入）

保留 `CACHE_CONFIRM_BEGIN/STATUS/RELEASE (75/76/77)` 和 kind=6 SignedCache 上传格式。确认请求与应答的字段不变；历史恢复不需要重新签名或通过设备 RF 发布。当前设备身份仍须与原内容作者相同，且提交和轮询绑定当前设备代次、上传 owner/ID/完整哈希。

确认后台首先验证内容作者签名和目录证明。对应 AuthorIssued 存在时必须匹配原作者、对象、版本及版本哈希；不存在时可由上述证明重建。草稿不存在时从签名内容恢复原 nonce、作者、base_hash 和编辑字段，初始本地草稿代次为 1；已有草稿保留，其他作者或损坏草稿拒绝。request/task、缺失作者记录与缺失草稿以同一索引事务提交，新增键全部按 absent-only 处理。空间不足、坏证明或冲突不得写入部分恢复记录。

私有 SD AuthorIssued 的普通值仍是 `[revision_hash, author_public_key, issued_at]`。经过验证的历史恢复值可为 `[revision_hash, author_public_key, issued_at, original_created_at]`；issued_at 的 UTC 取签名 updated_at、标为非可信设备时钟，original_created_at 来自同一签名内容。第 4 字段存在与数值 0 区分，且不得晚于 updated_at。后续版本预留据此检查原创建时间及父版本，避免伪造第 1 版哈希来续接历史。

新源码读取上述两种值；旧固件只读取 3 字段，不能承诺在写入恢复值后直接降级仍可读。网络 SignedCache v1、目录收据和 USB wire schema 保持原样。当前证据为原生密码学/索引事务及浏览器验证；SDK、真实 USB/SD、掉电及回退流程尚待验收。
