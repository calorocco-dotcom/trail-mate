# 存储入口归类与 Wi-Fi 保存失败修复

2026-10-13。本轮聚焦固件配置维护的可靠性，不扩展轨迹增强功能。源码已修改并通过相关原生测试；未进行 ESP SDK 整包编译或刷写。

## 已修复的配置保存问题

common Wi-Fi 与 P4/C6 IDF Wi-Fi 的原实现先保存配置数量，但没有检查 void 标量 setter 的结果；保存启用状态也未检查。网页完整配置应用经 replace_saved_profiles 调用这些实现，因此数量或启用状态未写入时可能仍返回成功。

两份实现现在先写完十个槽位的 SSID/密码，再写入并读回配置数量；任一槽位失败即停止槽位写入，数量不提前发布。启用状态使用与目标相反的默认值读回，缺失、读失败或不同值均使保存失败。保存失败使 profiles_cached/config_cached 失效，不能把内存中的候选当作已经持久化的配置。未使用槽位继续清空，零配置集合也清空旧主配置。common 集合入口补齐固定字符数组的 NUL 检查，与 IDF 行为保持一致。

旧 wifi_pass_{slot} 迁移之前忽略新 wifi_password_{slot} 写入结果并删除旧键。现在只有 put_string 确认成功才删除旧键；失败或结果不确定时保留旧密码，当前加载仍可使用它。已有新键继续优先，不因为发现旧键而覆盖。

settings_extension::finishDocument 的后续文本、blob、Wi-Fi、Reticulum 网络及板型配置应用改为遇到第一个失败立即返回。原逻辑在 Wi-Fi 失败后仍可能继续应用 Reticulum 网络配置。本修复减少失败后的后续修改，但不是跨 namespace 的 NVS 原子事务：失败前可能已修改部分键，SD 工作文档仍是重启恢复依据；真实掉电及 SD 恢复尚未验收。

没有新增常驻字段、凭据数组、线程或队列，复用现有运行时缓存及标量局部变量。新增原生测试的文件、map 和故障记录只存在于电脑测试进程。未重新链接 ESP 镜像，因此不报告本轮运行 RAM 或静态 RAM 新读数。

## 裸 NVS 入口归类

[43-裸 NVS 清单](43-裸NVS入口与存储别名清单.md) 扫描 1604 个仓库源文件，80 个候选调用位于 7 个文件。分类如下，不把调用次数当作配置数量：

| 来源 | 持久内容／用途 | 当前维护关系 |
|---|---|---|
| LxmfIdentity 与 identity_atomic_record.h | lxmf_ident/native_v1，176 字节记录含格式、代次、公私钥和 CRC | 现有 Reticulum 专用身份备份／恢复；CRC 是损坏检测，不代替公私钥对应与签名验证 |
| identity_key_state.h | native_v1 与四项旧公私钥的存在性检查 | 避免已存在但损坏的身份被当成空设备并自动生成另一个账号；不是新的配置集合 |
| identity_generation_store.h / identity_generation_recovery.h | tm_author/epoch，代次与身份哈希 | 身份切换及重启恢复的内部绑定；不开放手工编辑代次 |
| config_preferences.h | IDF 对 Arduino Preferences 标量／blob 类型的适配 | namespace/key 来自调用方，不另建配置事实来源；Arduino 分支仍使用系统 Preferences |
| settings_store_impl.h | 通用标量／字符串／blob 的 NVS 包装器及短键别名 | 追踪逻辑键的既有 TMS 投影，不能把短键再变成第二个编辑字段 |

## 18 个短键别名的覆盖关系

| 逻辑键 | 当前维护字段／用途 |
|---|---|
| display_language | 旧整数语言的一次迁移输入；当前维护 ui.locale |
| display_locale | ui.locale |
| screen_brightness | ui.display.brightness |
| vibration_enabled | ui.haptics.vibration_enabled |
| chat_message_alerts | chat.alerts.message |
| chat_contact_alerts | chat.alerts.contact |
| chat_auto_reply_enabled | chat.auto_reply.enabled |
| chat_auto_reply_text | chat.auto_reply.text |
| timezone_profile | ui.timezone.profile |
| gauge_design_mah | power.gauge.design_mah |
| wifi_profile_count | wifi.profile_count；由网页集合增删维护 |
| espnow_team_discovery | c6_companion.espnow_team_discovery，板型扩展 |
| espnow_beacon_interval_ms | c6_companion.espnow_beacon_interval_ms，板型扩展 |
| wifi_sta_enabled | c6_companion.wifi_sta_enabled，板型扩展 |
| wifi_sta_channel | c6_companion.wifi_sta_channel，板型扩展 |
| wifi_persist_credentials | c6_companion.wifi_persist_credentials，板型扩展 |
| wifi_sta_password | c6_companion.wifi_sta_password，板型扩展 |
| wifi_ap_password | c6_companion.wifi_ap_password，板型扩展 |

字段名依据当前 settings extension 与 tms_c6_config_extension.h。别名只在通用 settings_store 路径生效，不能假设直接 Preferences 自动使用它。板型块不出现在无对应硬件的设备文档中，不能因为 Wio 不显示就判断其它板型已覆盖实机验收。

## 当前 Wio 构建依赖的源码核对

依赖位置只在本机命令中使用，仓库记录包名／版本和相对源码。扫描不读取设备 NVS 或任何秘密值。

- 当前 Wio libdeps 的 3032 个 C/C++ 源文件中发现 19 个裸 NVS 候选，位于 Crypto/RNG.cpp 与 LovyanGFX 的 LGFX_AutoDetect_ESP32_all.hpp。
- Crypto 0.4.0 的 rng/seed 是随机数生成器的种子持久化，不是用户作者身份或可录入配置。ESP32 分支使用 NVS 与硬件随机源；当前 ELF map 确认 RNGClass::begin/save 已链接，项目 LXMF、Meshtastic 和语音密码学路径有初始化调用。不能把导出这个 seed 当作账号备份，也不加入普通配置表单。
- LovyanGFX 1.2.28 的 AUTODETECT 保存自动探测到的板型，是硬件缓存。当前仓库应用源码搜索没有发现自动探测类的调用；这是源码候选而非已证明 Wio 实际执行的存储路径。
- 当前 Arduino 框架包 3.20017.241212+sha.dcc1105b 的 libraries 扫描 198 个源文件、68 个候选，位于 Preferences.cpp 与 EEPROM.cpp。Preferences 的 namespace/key 由调用方提供，未新增用户配置。项目应用源码搜索未发现 EEPROM begin/read/write 调用，不能据此扩张为设备用户数据备份。
- common Wi-Fi 初始化显式调用 esp_wifi_set_storage(WIFI_STORAGE_RAM)，失败则退出初始化；SDK 的 Wi-Fi 内部保存不是这条路径的第二套用户配置来源。

以上只覆盖当前依赖目录可读源码，不自动覆盖 SDK 预编译内部实现、其它板型／依赖版本、蓝牙绑定运行数据、函数指针和宏展开。全部配置的整体结论仍须与旧 AppConfig 动态入口、RF 密钥管理及硬件实际生效分别闭环。

## 验证与后续

- CMake 从两份当前生产文件提取 persist_profiles、read_profile_from_store、保存入口和 replace_saved_profiles，使用真实 Config 类型；NVS 和运行时辅助函数模拟。分别覆盖 24 个写入位置的失败、两项标量读失败、启用／停用、空集合、非法未终止文本／重复 SSID／容量及旧密码迁移确定／不确定失败。
- 实际 finishDocument 生产函数测试覆盖 12 项标量写入／读回失败，以及随后六个文本／blob／Wi-Fi／Reticulum 调用逐项失败。失败后不再执行后面的 owner；其它准入依赖与存储仍是模拟。
- 4 项相关 CTest、115 项 config Node 测试、ESP 栈检查和 git diff --check 通过。新 CTest 和 config-nvs-access.test.mjs 被现有 CI 全目标／config 通配规则纳入；没有远程 CI 结果。
- NVS 扫描测试验证注释／字符串伪调用、嵌套实参、秘密值／输出缓冲不进入清单、别名重复／超长／不支持的项拒绝，以及只读 --check 一致性。通用存储清单更新为 167 显式调用、40 静态键、61 动态入口；这是新增读回造成的源码统计，不是新增六项用户配置。
- 本轮不改变 Reticulum 备份的范围，MeshCore／Meshtastic 专用密钥维护仍未补齐。不宣布“全部配置完成”。最新已编译候选仍 e5820533…，不含本轮修复；已刷固件仍 b733480e…，最近用户验收内存仍 87K/297K。保持集中编译与实机验收安排。
