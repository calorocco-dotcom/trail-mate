# 直接 Preferences 入口的覆盖核对

2026-10-12。依据 [25-直接 Preferences 入口清单](25-直接Preferences入口清单.md) 追踪实际调用，不按存储键数量计算完成百分比。下表记录普通配置、密码学身份、硬件派生标识、用户数据和操作标记的区别；这些类别不能互相代替验收。

| 实际来源 | 持久化入口 | 现有维护路径与边界 |
|---|---|---|
| device_identity::getSelfNodeId | chat/node_id | 由 eFuse MAC 后四字节派生，零时使用 1；存储值不符会被重新写回，不是用户可自由修改的配置。与网页使用的 Reticulum 密码学作者身份不同；不能把修改此整数当作迁移网页账号。 |
| app_config_sd_tms_runtime::reset_pending / set_reset_pending | cfgreset/working | 无 SD 恢复出厂操作的标记，防止之后插入旧卡恢复已重置配置；不开放为普通设置。配置恢复与工厂重置的实际掉电场景仍待验收。 |
| AppConfig 的传统 load/save 包装器 | chat/gps/settings/aprs 与动态频道键 | 当前直接扫描只能定位 Preferences 成员包装器，不能自动展开全部 get_*_logged/put_*_logged 实参。此前逐域核对见 [24](24-动态存储入口覆盖核对.md)；完整动态与旧缓存一致性仍保留。 |
| MeshCoreBleService::loadBlePin / saveBlePin | mc_ble/pin、manual_add、telem_base、telem_loc、telem_env、advert_loc | BLE extension 的 load_preferences/write_preferences 与这六个存储键一致，TMS 导出为 mc_ble.pin、manual_add_contacts、telemetry_base/location/environment、advert_location_policy。PIN 与遥测数值约束已有网页及固件校验；全部字段真实 BLE 生效尚未验收。 |
| MeshtasticBleService::loadBleConfig / saveBleConfig | mt_ble/enabled、mode、pin | TMS 对应 mt_ble.enabled、mode、fixed_pin。配置完整 presence、配对模式、六位 PIN 和 No PIN 要求已由 extension 校验，网页对应录入与备份约束。手机真实配对仍需验收。 |
| MeshtasticBleService::loadModuleConfig / saveModuleConfig | mt_mod/cfg | 原生 LocalModuleConfig 结构体及内部版本校验，TMS 经 protobuf 分块投影，网页结构化编辑。没有额外 CRC 键。BLE 与 TMS 共用模块规范化规则；模块每项实际功能仍待验收。 |
| MeshCoreBleService::loadManualContacts / saveManualContacts | mc_contacts/ver、blob | 联系人数据，版本 1，读取当前 ContactRecord 或旧布局并迁移。不是配置数量、频道集合或作者身份；当前三功能网页没有联系人数据管理，不能以配置备份宣称覆盖联系人。若要求迁移完整设备用户数据，应另行保留此需求。 |
| LxmfIdentity::loadFromPrefs / saveToPrefs | lxmf_ident 的四项原生公私钥，以及专用恢复记录 | 网页账号依托此密码学身份，通过专门证明、加密备份与恢复流程；独立于当前 RF 协议。实际硬件导出恢复、代次切换与掉电尚未验收。 |
| 通用 blob_store_io 包装器 | ns/key/version_key/crc_key 等实参 | 实际上层调用追踪见 [24](24-动态存储入口覆盖核对.md)。不能因为通用 API 支持 CRC，就推断所有存储都有 CRC；MeshCore、Meshtastic RF 身份备份也不能冒用 Reticulum 备份范围。 |

## MeshCore BLE 参数解释的限制

当前 meshcore_ble.cpp 的三项 telemetry_mode_* 与 advert_loc_policy_ 在读取、录入和保存路径有使用；此次没有找到依据这些成员执行遥测筛选或位置广播的运行时分支。网页应保持源码支持的数值约束，不能把 0–3 或 0–255 编造成未经证明的隐私策略选项，也不能以保存读回代替功能实际生效。manual_add_contacts_ 则需沿实际联系人处理分支单独验收。

## 清单验证与未覆盖范围

直接扫描 1216 个源文件，候选 130 个调用，包括 24 个 namespace 开启、106 个键访问；39 个字面量键访问、67 个常量/动态表达式。统计是调用次数，不是独立配置数量。脚本测试覆盖省略默认值/秘密值、注释及字符串伪调用、引用参数、嵌套实参和多行调用；生成后 --check 通过。

清单仍不涵盖裸 NVS API、第三方库存储、跨文件 receiver 别名、箭头访问或编译器实际条件展开。以上对账是源码证据，没有读取设备值，没有固件构建或上传，也不改变 RAM。配置维护最终验收继续以 [16](16-交付缺口核对.md) 的完整范围为准。
