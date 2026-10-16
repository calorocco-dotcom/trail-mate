# 平台配置读取与BLE保存失败修复

2026-10-13，接44与55。继续核对独立配置的读取／应用失败路径；本批不新增网页账号、RF协议、串口操作或用户配置字段。

## BLE保存的首次失败边界

实际write_preferences依次写mt_ble三项、mc_ble六项、mt_mod模块blob。旧代码把失败保留到最终返回值，但mt_ble或mc_ble失败后仍打开后面的namespace并写入。这不会报告成功，却扩大了失败后的局部修改范围。

新增测试提取实际函数，使用真实生成BluetoothConfig／LocalModuleConfig类型，替换NVS适配器为调用观察器。修复前逐项写失败断言复现“失败后仍继续写”；修复后每组关闭句柄即检查结果，失败直接返回，后面的owner不再执行。

10个写入位置及3个namespace打开失败均验证不执行后续写入，已打开句柄正常关闭；合法false／0以及模块实际sizeof保持原行为。NVS持久化由测试替身模拟，不据此证明真实掉电或跨键原子性。

## 配置读取异常不能变成空值

write_text_setting、write_string_as_blob、write_blob_setting原来把有界读取返回false一律当成空值。语言、输入法列表／自动回复文本、时区blob遇到超长、类型错误或存储不可用时，可能在快照／工作配置投影中变成空字段。

现在文本投影使用已有StringReadResult，只对Missing使用空值，Invalid／Unavailable立即停止。新增ESP严格BlobReadResult接口，区分Ok／Missing／Invalid／Unavailable；零长度blob为合法Ok，只有Missing可使用默认值。旧get_blob_into接口及其调用方保持原契约。

严格blob读取使用调用方缓冲，先测长度再有界读取；超容量不读内容，错误或两次长度变化不发布结果，输出长度保持0。读入后失败清零已尝试的区域，不输出缓冲／秘密内容。所有NVS成功打开分支均关闭句柄，别名解析沿用现有规则。

本接口为ESP配置投影使用，与已有严格字符串接口相同；Linux通用设置存储的完整严格读取语义尚不在本批范围。当前标量get_int／get_bool／get_uint仍有默认回退语义，也不能凭本批文本／blob测试宣布所有存储异常均已覆盖。

## 原生验证与资源

- 修复前实际投影函数复现读取Invalid／Unavailable被导出为空；修复后有效值／明确缺失可导出，异常在输出字段前停止。
- 8项相关CTest通过：BLE保存、严格文本／blob读取、三种投影、标量应用、common／IDF Wi-Fi保存、C6存储。NVS／平台读取模拟，投影使用实际RecordWriter与核心codec；不是实机NVS／SD／网络验证。
- blob读取覆盖namespace不存在／不可用、键不存在／类型错误、超长、不完整／长度变化、零长度及二进制中的零字节；文本投影覆盖容量与无新输出。
- 配置Node回归128项、clang-format14、ESP栈检查及差异检查通过。原生目标由现有CI全目标／CTest入口覆盖，尚未远程执行。
- 存储清单更新：22仍167显式调用／40静态键／61动态入口，新增严格blob方法纳入扫描；25为130直接Preferences调用；43为1607源文件／104裸NVS候选／18别名。新增读取调用属于基础设施，不是新增用户配置。47保存投影138项／0未解析保持。
- GitNexus报告BLE writer LOW、一个直接finishDocument调用者；三个投影函数LOW且图中未列调用者，已补查writeRecords。新blob接口／扫描函数UNKNOWN，手工核对所有新调用，未忽略图缺口。

复用已有ParseState的192字节暂存，不增加队列、任务、常驻缓冲或复制完整配置。合并上一轮54的主循环调度修复，仅启动一次Wio L2 SDK候选构建：PID32936退出0，SUCCESS208.91秒，stderr为空；静态RAM仍95,556字节，Flash4,798,413（比上一未刷候选减少188字节），bin4,798,832，SHA256为`f233850df241cd3fcd62328dd92ebc5cd5b46c541dd77c7c05f3d2c4d04bd863`。

实际镜像56项管理能力、6MiB分区预算核对通过；ELF包含新的严格blob读取、BLE配置应用、设置投影和主循环配置服务，构建日志确认三个实际配置实现重新编译。已保留uploaded镜像／ELF仍为8bcf6607…且哈希复核；没有增加候选历史副本或刷写。用户87K／297K正常运行反馈为此前已刷版本，不是新候选运行测量。

候选验证结束时只读端口枚举为COM67，COM69未出现；没有操作设备复位或写flash。该枚举不证明新候选已运行，也不作为配置／USB重连实测。

全部配置加载／运行时消费、真实BLE／Wi-Fi生效、跨板型、身份／SD／掉电、软件重启USB、Geocaching及轨迹完整联网维护等按16继续保留。
