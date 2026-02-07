# Guía de Usuario - Sistema de Configuración Multidispositivo

## Descripción General

Este sistema permite configurar cada dispositivo ESP32-S3 de forma individual, y la configuración se mantiene incluso después de actualizar el firmware (OTA o USB). Puedes configurar:

- **Identificación del dispositivo** (ID y nombre)
- **WiFi** (SSID y contraseña)
- **MQTT** (servidor, puerto, credenciales, topics)
- **Pines GPIO** (lectores RC522, relés, buzzer, lector QR)
- **Funcionalidades** (habilitar/deshabilitar lectores, WiFi, MQTT)

## Configuración Vía MQTT

### 1. Obtener Configuración Actual

Envía un mensaje MQTT al topic del dispositivo con:

```json
{
  "action": "getConfig",
  "idPeticion": "req-001"
}
```

Respuesta en topic `/var/deploys/topics/SFTCLUB`:

```json
{
  "action": "retornoConfig",
  "id": "SFTCLUB_DEVICE",
  "idPeticion": "req-001",
  "version": 2,
  "deviceId": "SFTCLUB_DEVICE",
  "deviceName": "Default Device",
  "enableCards": false,
  "enableQr": true,
  "enableWifi": true,
  "enableMqtt": true,
  "wifiSsid": "DIGIFIBRA-3SDH",
  "mqttHost": "mqtt.pro.wiplaypadel.com",
  "mqttPort": 1883,
  "mqttUser": "admin",
  "mqttTopicRoot": "/var/deploys/topics",
  "gpioRc522": {
    "mosi": 11,
    "miso": 13,
    "sck": 12,
    "ss1": 10,
    "rst1": 16,
    "ss2": 15,
    "rst2": 17
  },
  "tornInPin": 19,
  "tornOutPin": 20,
  "buzzerPin": 21,
  "gpioQr": {
    "tx": 17,
    "rx": 18,
    "uartNum": 1,
    "baudRate": 9600
  }
}
```

### 2. Actualizar Configuración

Envía un mensaje MQTT con los valores que deseas cambiar:

```json
{
  "action": "setConfig",
  "idPeticion": "req-002",
  "config": {
    "deviceId": "CLUB_DISPOSITIVO_01",
    "deviceName": "Torniquete Entrada Principal",
    "enableCards": true,
    "enableQr": true,
    "wifiSsid": "Mi_Red_WiFi",
    "wifiPass": "mi_contraseña_segura",
    "mqttHost": "mi.servidor.mqtt.com",
    "mqttPort": 1883,
    "tornInPin": 19,
    "tornOutPin": 20
  }
}
```

**Nota:** Solo incluye los campos que quieres cambiar. Los demás mantienen su valor actual.

Respuesta:

```json
{
  "action": "retornoSetConfig",
  "ok": true,
  "message": "Config saved",
  "needsRestart": true,
  "enableCards": true,
  "enableQr": true,
  "idPeticion": "req-002",
  "id": "CLUB_DISPOSITIVO_01"
}
```

**Importante:** Si `needsRestart` es `true`, debes reiniciar el dispositivo para que ciertos cambios (WiFi, MQTT, pines GPIO) tengan efecto.

### 3. Restablecer a Valores por Defecto

Para volver a la configuración de fábrica:

```json
{
  "action": "resetConfig",
  "idPeticion": "req-003"
}
```

Respuesta:

```json
{
  "action": "retornoResetConfig",
  "ok": true,
  "message": "Config reset to defaults",
  "idPeticion": "req-003",
  "id": "SFTCLUB_DEVICE"
}
```

## Campos Configurables

### Identificación del Dispositivo

| Campo | Tipo | Descripción | Ejemplo |
|-------|------|-------------|---------|
| `deviceId` | String (32 chars) | ID único del dispositivo | "CLUB_TORNO_01" |
| `deviceName` | String (64 chars) | Nombre descriptivo | "Torniquete Principal" |

### Funcionalidades

| Campo | Tipo | Descripción | Default |
|-------|------|-------------|---------|
| `enableCards` | Boolean | Habilitar lectores de tarjetas RC522 | false |
| `enableQr` | Boolean | Habilitar lector de códigos QR | true |
| `enableWifi` | Boolean | Habilitar WiFi | true |
| `enableMqtt` | Boolean | Habilitar MQTT | true |

### WiFi

| Campo | Tipo | Descripción | Ejemplo |
|-------|------|-------------|---------|
| `wifiSsid` | String (64 chars) | Nombre de la red WiFi | "MiRedWiFi" |
| `wifiPass` | String (64 chars) | Contraseña WiFi | "Password123" |

**Nota de Seguridad:** La contraseña NO se devuelve en `getConfig` por seguridad.

### MQTT

| Campo | Tipo | Descripción | Ejemplo |
|-------|------|-------------|---------|
| `mqttHost` | String (128 chars) | Servidor MQTT | "mqtt.miservidor.com" |
| `mqttPort` | Integer | Puerto MQTT (1-65535) | 1883 |
| `mqttUser` | String (64 chars) | Usuario MQTT | "admin" |
| `mqttPass` | String (64 chars) | Contraseña MQTT | "MiPass123" |
| `mqttTopicRoot` | String (128 chars) | Raíz de topics MQTT | "/var/deploys/topics" |

**Nota de Seguridad:** La contraseña NO se devuelve en `getConfig` por seguridad.

### Pines GPIO - Lectores RC522 (SPI)

Los lectores RC522 usan bus SPI compartido con pines individuales de SS y RST por lector:

| Campo | Tipo | Descripción | Default |
|-------|------|-------------|---------|
| `gpioRc522.mosi` | Integer (0-48) | Pin MOSI (SPI) | 11 |
| `gpioRc522.miso` | Integer (0-48) | Pin MISO (SPI) | 13 |
| `gpioRc522.sck` | Integer (0-48) | Pin SCK (SPI) | 12 |
| `gpioRc522.ss1` | Integer (0-48) | Pin SS lector 1 | 10 |
| `gpioRc522.rst1` | Integer (0-48) | Pin RST lector 1 | 16 |
| `gpioRc522.ss2` | Integer (0-48) | Pin SS lector 2 | 15 |
| `gpioRc522.rst2` | Integer (0-48) | Pin RST lector 2 | 17 |

### Pines GPIO - Relés y Buzzer

| Campo | Tipo | Descripción | Default |
|-------|------|-------------|---------|
| `tornInPin` | Integer (0-48) | Pin relé entrada | 19 |
| `tornOutPin` | Integer (0-48) | Pin relé salida | 20 |
| `buzzerPin` | Integer (0-48) | Pin buzzer | 21 |

### Pines GPIO - Lector QR (UART)

| Campo | Tipo | Descripción | Default |
|-------|------|-------------|---------|
| `gpioQr.tx` | Integer (0-48) | Pin TX UART | 17 |
| `gpioQr.rx` | Integer (0-48) | Pin RX UART | 18 |
| `gpioQr.uartNum` | Integer (0-2) | Número de UART | 1 |
| `gpioQr.baudRate` | Integer | Velocidad baudios | 9600 |

## Ejemplos de Uso

### Configurar un Dispositivo Nuevo

1. **Conecta el dispositivo** y espera a que arranque con la configuración por defecto

2. **Cambia el ID y nombre:**
```json
{
  "action": "setConfig",
  "config": {
    "deviceId": "CLUB_RECEPCION",
    "deviceName": "Torniquete Recepción"
  }
}
```

3. **Configura el WiFi:**
```json
{
  "action": "setConfig",
  "config": {
    "wifiSsid": "WiFi_Club",
    "wifiPass": "ClaveSegura123"
  }
}
```

4. **Habilita los lectores necesarios:**
```json
{
  "action": "setConfig",
  "config": {
    "enableCards": true,
    "enableQr": true
  }
}
```

5. **Reinicia el dispositivo** para que los cambios tengan efecto

### Configurar Pines GPIO Personalizados

Si tu hardware usa pines diferentes:

```json
{
  "action": "setConfig",
  "config": {
    "gpioRc522": {
      "mosi": 23,
      "miso": 19,
      "sck": 18,
      "ss1": 5,
      "rst1": 22,
      "ss2": 21,
      "rst2": 17
    },
    "tornInPin": 25,
    "tornOutPin": 26,
    "buzzerPin": 27
  }
}
```

**Reinicia** el dispositivo después de cambiar los pines GPIO.

### Cambiar Servidor MQTT

```json
{
  "action": "setConfig",
  "config": {
    "mqttHost": "nuevo.servidor.com",
    "mqttPort": 8883,
    "mqttUser": "dispositivo01",
    "mqttPass": "NuevaContraseña",
    "mqttTopicRoot": "/club/dispositivos"
  }
}
```

**Reinicia** el dispositivo después de cambiar la configuración MQTT.

## Persistencia de la Configuración

### ¿Qué se Guarda?

La configuración se guarda en la **partición NVS** (Non-Volatile Storage) del ESP32, que es independiente de las particiones de firmware.

### ¿Cuándo se Mantiene la Configuración?

✅ **SE MANTIENE** en estos casos:
- Actualizaciones OTA (Over-The-Air)
- Flasheo USB con nuevo firmware (si no se borra explícitamente)
- Reinicios normales del dispositivo
- Pérdidas de alimentación

❌ **SE PIERDE** en estos casos:
- Flasheo completo con borrado de flash (`idf.py erase-flash`)
- Comando explícito de reset a defaults (`resetConfig`)
- Corrupción de la partición NVS (muy raro)

### ¿Cómo Preservar la Configuración al Flashear?

Al flashear vía USB con ESP-IDF:

```bash
# CORRECTO: Mantiene NVS
idf.py flash

# INCORRECTO: Borra todo incluyendo NVS
idf.py erase-flash
idf.py flash
```

## Migración de Versiones

El sistema detecta automáticamente cambios en la versión de configuración y migra los valores existentes:

1. Al arrancar, carga la configuración de NVS
2. Si la versión no coincide:
   - Carga los valores por defecto
   - Restaura los valores personalizados del usuario (WiFi, MQTT, device ID, etc.)
   - Guarda la nueva versión
3. Continúa el arranque normalmente

**Logs de migración:**
```
I (1234) APP_CFG: Config version mismatch (stored:1, expected:2), migrating...
I (1235) APP_CFG: Config saved to NVS
I (1236) APP_CFG: Config migrated to version 2
```

## Solución de Problemas

### El dispositivo no se conecta después de cambiar WiFi

1. Verifica que el SSID y contraseña sean correctos
2. Asegúrate de que el WiFi esté habilitado: `"enableWifi": true`
3. Reinicia el dispositivo
4. Si persiste, usa `resetConfig` para volver a defaults

### Los cambios no tienen efecto

Algunos cambios requieren reinicio:
- Configuración WiFi
- Configuración MQTT
- Pines GPIO

Verifica en la respuesta de `setConfig` si `needsRestart` es `true`.

### El dispositivo no responde a comandos MQTT

1. Verifica que MQTT esté habilitado: `"enableMqtt": true`
2. Verifica la configuración del servidor MQTT
3. Revisa los logs del dispositivo por errores de conexión
4. Verifica que el topic sea correcto

### Los lectores de tarjetas no funcionan

1. Verifica que estén habilitados: `"enableCards": true`
2. Verifica que los pines GPIO sean correctos
3. Reinicia el dispositivo después de cambiar pines
4. Verifica el cableado del hardware

## Seguridad

### Contraseñas

- Las contraseñas WiFi y MQTT **NO** se devuelven en `getConfig`
- Se almacenan cifradas en NVS
- Solo se actualizan cuando se envían en `setConfig`

### Recomendaciones

1. **Usa contraseñas seguras** para WiFi y MQTT
2. **Limita el acceso** a los topics MQTT del dispositivo
3. **No expongas** el broker MQTT a Internet sin TLS
4. **Actualiza el firmware** regularmente vía OTA

## Monitoreo

### Ver Configuración Actual

Puedes solicitar `getConfig` en cualquier momento para ver la configuración activa.

### Logs del Dispositivo

Para desarrollo, conecta por USB y monitorea:

```bash
idf.py monitor
```

Verás logs como:
```
I (1234) APP_CFG: Config loaded from NVS (version 2)
I (1235) TOTPADEL: DEVICE_ID=CLUB_RECEPCION
I (1236) TOTPADEL: DEVICE_NAME=Torniquete Recepción
I (1237) WIFI: Using WiFi SSID=WiFi_Club
I (1238) MQTT: MQTT started with broker: mqtt.pro.wiplaypadel.com
```

## Soporte

Para más información técnica, consulta:
- `CONFIG_DEVELOPER_GUIDE.md` - Guía para desarrolladores
- `CODE_REVIEW.md` - Revisión completa del código
- `FIXES_APPLIED.md` - Cambios y mejoras aplicadas
