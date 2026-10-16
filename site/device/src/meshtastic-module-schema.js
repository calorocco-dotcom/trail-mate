// Generated from checked-in nanopb headers; run scripts/generate_meshtastic_module_schema.mjs.
export const MODULE_SCHEMA={
  "root": "meshtastic_LocalModuleConfig",
  "messages": {
    "meshtastic_LocalModuleConfig": {
      "maxBytes": 673,
      "fields": [
        {
          "name": "mqtt",
          "number": 1,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_MQTTConfig"
        },
        {
          "name": "serial",
          "number": 2,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_SerialConfig"
        },
        {
          "name": "external_notification",
          "number": 3,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_ExternalNotificationConfig"
        },
        {
          "name": "store_forward",
          "number": 4,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_StoreForwardConfig"
        },
        {
          "name": "range_test",
          "number": 5,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_RangeTestConfig"
        },
        {
          "name": "telemetry",
          "number": 6,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_TelemetryConfig"
        },
        {
          "name": "canned_message",
          "number": 7,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_CannedMessageConfig"
        },
        {
          "name": "version",
          "number": 8,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295,
          "readOnly": true
        },
        {
          "name": "audio",
          "number": 9,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_AudioConfig"
        },
        {
          "name": "remote_hardware",
          "number": 10,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_RemoteHardwareConfig"
        },
        {
          "name": "neighbor_info",
          "number": 11,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_NeighborInfoConfig"
        },
        {
          "name": "ambient_lighting",
          "number": 12,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_AmbientLightingConfig"
        },
        {
          "name": "detection_sensor",
          "number": 13,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_DetectionSensorConfig"
        },
        {
          "name": "paxcounter",
          "number": 14,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_PaxcounterConfig"
        }
      ]
    },
    "meshtastic_ModuleConfig_MQTTConfig": {
      "maxBytes": 224,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "address",
          "number": 2,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 63
        },
        {
          "name": "username",
          "number": 3,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 63
        },
        {
          "name": "password",
          "number": 4,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 31
        },
        {
          "name": "encryption_enabled",
          "number": 5,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "json_enabled",
          "number": 6,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "tls_enabled",
          "number": 7,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "root",
          "number": 8,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 31
        },
        {
          "name": "proxy_to_client_enabled",
          "number": 9,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "map_reporting_enabled",
          "number": 10,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "map_report_settings",
          "number": 11,
          "type": "MESSAGE",
          "cardinality": "OPTIONAL",
          "message": "meshtastic_ModuleConfig_MapReportSettings"
        }
      ]
    },
    "meshtastic_ModuleConfig_MapReportSettings": {
      "maxBytes": 14,
      "fields": [
        {
          "name": "publish_interval_secs",
          "number": 1,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "position_precision",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "should_report_location",
          "number": 3,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_SerialConfig": {
      "maxBytes": 28,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "echo",
          "number": 2,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "rxd",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "txd",
          "number": 4,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "baud",
          "number": 5,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "BAUD_DEFAULT",
              "value": 0
            },
            {
              "name": "BAUD_110",
              "value": 1
            },
            {
              "name": "BAUD_300",
              "value": 2
            },
            {
              "name": "BAUD_600",
              "value": 3
            },
            {
              "name": "BAUD_1200",
              "value": 4
            },
            {
              "name": "BAUD_2400",
              "value": 5
            },
            {
              "name": "BAUD_4800",
              "value": 6
            },
            {
              "name": "BAUD_9600",
              "value": 7
            },
            {
              "name": "BAUD_19200",
              "value": 8
            },
            {
              "name": "BAUD_38400",
              "value": 9
            },
            {
              "name": "BAUD_57600",
              "value": 10
            },
            {
              "name": "BAUD_115200",
              "value": 11
            },
            {
              "name": "BAUD_230400",
              "value": 12
            },
            {
              "name": "BAUD_460800",
              "value": 13
            },
            {
              "name": "BAUD_576000",
              "value": 14
            },
            {
              "name": "BAUD_921600",
              "value": 15
            }
          ]
        },
        {
          "name": "timeout",
          "number": 6,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "mode",
          "number": 7,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "DEFAULT",
              "value": 0
            },
            {
              "name": "SIMPLE",
              "value": 1
            },
            {
              "name": "PROTO",
              "value": 2
            },
            {
              "name": "TEXTMSG",
              "value": 3
            },
            {
              "name": "NMEA",
              "value": 4
            },
            {
              "name": "CALTOPO",
              "value": 5
            },
            {
              "name": "WS85",
              "value": 6
            },
            {
              "name": "VE_DIRECT",
              "value": 7
            },
            {
              "name": "MS_CONFIG",
              "value": 8
            }
          ]
        },
        {
          "name": "override_console_serial_port",
          "number": 8,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_ExternalNotificationConfig": {
      "maxBytes": 42,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "output_ms",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "output",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "active",
          "number": 4,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "alert_message",
          "number": 5,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "alert_bell",
          "number": 6,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "use_pwm",
          "number": 7,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "output_vibra",
          "number": 8,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "output_buzzer",
          "number": 9,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "alert_message_vibra",
          "number": 10,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "alert_message_buzzer",
          "number": 11,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "alert_bell_vibra",
          "number": 12,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "alert_bell_buzzer",
          "number": 13,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "nag_timeout",
          "number": 14,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 65535
        },
        {
          "name": "use_i2s_as_buzzer",
          "number": 15,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_StoreForwardConfig": {
      "maxBytes": 24,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "heartbeat",
          "number": 2,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "records",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "history_return_max",
          "number": 4,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "history_return_window",
          "number": 5,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "is_server",
          "number": 6,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_RangeTestConfig": {
      "maxBytes": 12,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "sender",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "save",
          "number": 3,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "clear_on_reboot",
          "number": 4,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_TelemetryConfig": {
      "maxBytes": 48,
      "fields": [
        {
          "name": "device_update_interval",
          "number": 1,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "environment_update_interval",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "environment_measurement_enabled",
          "number": 3,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "environment_screen_enabled",
          "number": 4,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "environment_display_fahrenheit",
          "number": 5,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "air_quality_enabled",
          "number": 6,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "air_quality_interval",
          "number": 7,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "power_measurement_enabled",
          "number": 8,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "power_update_interval",
          "number": 9,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "power_screen_enabled",
          "number": 10,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "health_measurement_enabled",
          "number": 11,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "health_update_interval",
          "number": 12,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "health_screen_enabled",
          "number": 13,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "device_telemetry_enabled",
          "number": 14,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_CannedMessageConfig": {
      "maxBytes": 49,
      "fields": [
        {
          "name": "rotary1_enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "inputbroker_pin_a",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "inputbroker_pin_b",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "inputbroker_pin_press",
          "number": 4,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "inputbroker_event_cw",
          "number": 5,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "NONE",
              "value": 0
            },
            {
              "name": "UP",
              "value": 17
            },
            {
              "name": "DOWN",
              "value": 18
            },
            {
              "name": "LEFT",
              "value": 19
            },
            {
              "name": "RIGHT",
              "value": 20
            },
            {
              "name": "SELECT",
              "value": 10
            },
            {
              "name": "BACK",
              "value": 27
            },
            {
              "name": "CANCEL",
              "value": 24
            }
          ]
        },
        {
          "name": "inputbroker_event_ccw",
          "number": 6,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "NONE",
              "value": 0
            },
            {
              "name": "UP",
              "value": 17
            },
            {
              "name": "DOWN",
              "value": 18
            },
            {
              "name": "LEFT",
              "value": 19
            },
            {
              "name": "RIGHT",
              "value": 20
            },
            {
              "name": "SELECT",
              "value": 10
            },
            {
              "name": "BACK",
              "value": 27
            },
            {
              "name": "CANCEL",
              "value": 24
            }
          ]
        },
        {
          "name": "inputbroker_event_press",
          "number": 7,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "NONE",
              "value": 0
            },
            {
              "name": "UP",
              "value": 17
            },
            {
              "name": "DOWN",
              "value": 18
            },
            {
              "name": "LEFT",
              "value": 19
            },
            {
              "name": "RIGHT",
              "value": 20
            },
            {
              "name": "SELECT",
              "value": 10
            },
            {
              "name": "BACK",
              "value": 27
            },
            {
              "name": "CANCEL",
              "value": 24
            }
          ]
        },
        {
          "name": "updown1_enabled",
          "number": 8,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "enabled",
          "number": 9,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "allow_input_source",
          "number": 10,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 15
        },
        {
          "name": "send_bell",
          "number": 11,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_AudioConfig": {
      "maxBytes": 19,
      "fields": [
        {
          "name": "codec2_enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "ptt_pin",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "bitrate",
          "number": 3,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "CODEC2_DEFAULT",
              "value": 0
            },
            {
              "name": "CODEC2_3200",
              "value": 1
            },
            {
              "name": "CODEC2_2400",
              "value": 2
            },
            {
              "name": "CODEC2_1600",
              "value": 3
            },
            {
              "name": "CODEC2_1400",
              "value": 4
            },
            {
              "name": "CODEC2_1300",
              "value": 5
            },
            {
              "name": "CODEC2_1200",
              "value": 6
            },
            {
              "name": "CODEC2_700",
              "value": 7
            },
            {
              "name": "CODEC2_700B",
              "value": 8
            }
          ]
        },
        {
          "name": "i2s_ws",
          "number": 4,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "i2s_sd",
          "number": 5,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "i2s_din",
          "number": 6,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "i2s_sck",
          "number": 7,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        }
      ]
    },
    "meshtastic_ModuleConfig_RemoteHardwareConfig": {
      "maxBytes": 96,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "allow_undefined_pin_access",
          "number": 2,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "available_pins",
          "number": 3,
          "type": "MESSAGE",
          "cardinality": "REPEATED",
          "message": "meshtastic_RemoteHardwarePin",
          "maxCount": 4
        }
      ]
    },
    "meshtastic_RemoteHardwarePin": {
      "maxBytes": 21,
      "fields": [
        {
          "name": "gpio_pin",
          "number": 1,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "name",
          "number": 2,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 14
        },
        {
          "name": "type",
          "number": 3,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "UNKNOWN",
              "value": 0
            },
            {
              "name": "DIGITAL_READ",
              "value": 1
            },
            {
              "name": "DIGITAL_WRITE",
              "value": 2
            }
          ]
        }
      ]
    },
    "meshtastic_ModuleConfig_NeighborInfoConfig": {
      "maxBytes": 10,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "update_interval",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "transmit_over_lora",
          "number": 3,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_AmbientLightingConfig": {
      "maxBytes": 14,
      "fields": [
        {
          "name": "led_state",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "current",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "red",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "green",
          "number": 4,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "blue",
          "number": 5,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        }
      ]
    },
    "meshtastic_ModuleConfig_DetectionSensorConfig": {
      "maxBytes": 44,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "minimum_broadcast_secs",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "state_broadcast_secs",
          "number": 3,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "send_bell",
          "number": 4,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "name",
          "number": 5,
          "type": "STRING",
          "cardinality": "SINGULAR",
          "maxBytes": 19
        },
        {
          "name": "monitor_pin",
          "number": 6,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 255
        },
        {
          "name": "detection_trigger_type",
          "number": 7,
          "type": "UENUM",
          "cardinality": "SINGULAR",
          "options": [
            {
              "name": "LOGIC_LOW",
              "value": 0
            },
            {
              "name": "LOGIC_HIGH",
              "value": 1
            },
            {
              "name": "FALLING_EDGE",
              "value": 2
            },
            {
              "name": "RISING_EDGE",
              "value": 3
            },
            {
              "name": "EITHER_EDGE_ACTIVE_LOW",
              "value": 4
            },
            {
              "name": "EITHER_EDGE_ACTIVE_HIGH",
              "value": 5
            }
          ]
        },
        {
          "name": "use_pullup",
          "number": 8,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        }
      ]
    },
    "meshtastic_ModuleConfig_PaxcounterConfig": {
      "maxBytes": 30,
      "fields": [
        {
          "name": "enabled",
          "number": 1,
          "type": "BOOL",
          "cardinality": "SINGULAR"
        },
        {
          "name": "paxcounter_update_interval",
          "number": 2,
          "type": "UINT32",
          "cardinality": "SINGULAR",
          "max": 4294967295
        },
        {
          "name": "wifi_threshold",
          "number": 3,
          "type": "INT32",
          "cardinality": "SINGULAR",
          "min": -2147483648,
          "max": 2147483647
        },
        {
          "name": "ble_threshold",
          "number": 4,
          "type": "INT32",
          "cardinality": "SINGULAR",
          "min": -2147483648,
          "max": 2147483647
        }
      ]
    }
  }
};
