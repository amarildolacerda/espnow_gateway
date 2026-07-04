# ESP8266 Rain Sensor Client (ESP-NOW)

Cliente ESP8266 para sensor de chuva. Comunica com o **ESP-NOW Gateway** via ESP-NOW, que encaminha os dados ao bridge via HTTP.

## Funcionalidades

- Leitura de sensor de chuva via entrada analógica (ADC) + digital
- LED de status (WiFi, gateway, config portal)
- Comandos via terminal serial (pressione 'h' para ajuda)
- Pareamento automático com gateway ESP-NOW
- Envio de dados via ESP-NOW
- Heartbeat periódico via ESP-NOW
- Servidor web embarcado com dashboard
- Portal de configuração WiFi (WiFiManager)
- OTA via ArduinoOTA e upload HTTP (`/api/ota`)

## Hardware

| Componente         | GPIO |
|--------------------|------|
| Sensor (analógico) | A0   |
| Sensor (digital)   | 16   |
| LED                | 2    |

## Faixas de leitura

O ADC 0–1024 é mapeado linearmente para 0–100%.

| % chuva   | Classificação |
|-----------|---------------|
| 0–10%     | Seco          |
| 11–40%    | Chuviscando   |
| 41–70%    | Chovendo      |
| 71–100%   | Chuva forte   |

## Configuração

Edite `include/config.h`:

```cpp
#define DEVICE_NAME "Rain ESP-NOW"
#define ESPNOW_CHANNEL 1
```

## Build (PlatformIO)

```bash
pio run -t upload
pio device monitor
```

## API local (no ESP8266)

| Rota              | Método | Descrição                           |
|-------------------|--------|-------------------------------------|
| `/`               | GET    | Dashboard web                       |
| `/api/state`      | GET    | Estado atual (rain_level, rain_digital) |
| `/api/pin?gpio=N` | GET    | Estado digital do pino GPIO N       |
| `/api/pin`        | POST   | Controla pino: `{"gpio":N,"state":1|0}` |
| `/api/ota`        | POST   | Upload de firmware via OTA (multipart) |

## ESP-NOW Protocol

O cliente implementa o protocolo `espnow_protocol.h`:

1. **Pairing**: envia `ESPNOW_MSG_PAIR_REQUEST` (broadcast), gateway responde com `ESPNOW_MSG_PAIR_RESPONSE`
2. **Dados**: envia `ESPNOW_MSG_SENSOR_DATA` com payload `payload_rain_t`
3. **Heartbeat**: envia `ESPNOW_MSG_HEARTBEAT` periodicamente
4. **ACK**: gateway responde com `ESPNOW_MSG_ACK`

MAC do gateway é salvo em EEPROM para persistência entre reboots.

## OTA

Via ArduinoOTA (hostname: `{device_id}.local`, porta 8266):

```bash
pio run -t upload --upload-port esp8266_xxxxxx.local
```

Via upload HTTP:

```bash
curl -F "firmware=@.pio/build/nodemcuv2/firmware.bin" http://esp8266_xxxxxx.local/api/ota
```

## Atalhos de teclado (terminal serial)

- `l` — ler sensor agora
- `s` — status do dispositivo
- `r` — restart
- `p` — resetar par e tentar parear com gateway
- `u` — info OTA
- `h/?` — ajuda

## Dependências (PlatformIO)

- `bblanchon/ArduinoJson` ^7.3.1
- `tzapu/WiFiManager` ^2.0.0
