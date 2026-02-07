# Sistema de Configuración Multidispositivo ESP32-S3

## 🎯 Resumen

Este proyecto ahora incluye un **sistema completo de configuración multidispositivo** que permite:

- ✅ Configurar cada ESP32-S3 de forma individual
- ✅ La configuración **persiste a través de actualizaciones OTA y USB**
- ✅ Configuración **en tiempo de ejecución vía comandos MQTT**
- ✅ Soporte para configurar: WiFi, MQTT, pines GPIO, funcionalidades

## 📋 ¿Qué Puedes Configurar?

### Identificación
- **ID del dispositivo** (ej: "CLUB_TORNO_01")
- **Nombre del dispositivo** (ej: "Torniquete Entrada")

### Conectividad
- **WiFi:** SSID y contraseña
- **MQTT:** servidor, puerto, usuario, contraseña, topics

### Hardware (Pines GPIO)
- **Lectores RC522:** pines SPI (MOSI, MISO, SCK, SS, RST) para 2 lectores
- **Relés:** pins de entrada y salida
- **Buzzer:** pin del zumbador
- **Lector QR:** pines UART (TX, RX), número de puerto, baudios

### Funcionalidades
- **Habilitar/deshabilitar** lectores de tarjetas
- **Habilitar/deshabilitar** lector QR
- **Habilitar/deshabilitar** WiFi
- **Habilitar/deshabilitar** MQTT

## 🚀 Inicio Rápido

### 1. Obtener Configuración Actual

Envía vía MQTT al topic del dispositivo:

```json
{
  "action": "getConfig",
  "idPeticion": "req-001"
}
```

### 2. Modificar Configuración

Envía los campos que quieres cambiar:

```json
{
  "action": "setConfig",
  "idPeticion": "req-002",
  "config": {
    "deviceId": "MI_DISPOSITIVO_01",
    "deviceName": "Torniquete Principal",
    "enableCards": true,
    "wifiSsid": "Mi_WiFi",
    "wifiPass": "MiContraseña"
  }
}
```

### 3. Reiniciar si es Necesario

Si la respuesta incluye `"needsRestart": true`, reinicia el dispositivo para aplicar cambios críticos (WiFi, MQTT, GPIO).

## 📚 Documentación Completa

- **[CONFIG_USER_GUIDE.md](CONFIG_USER_GUIDE.md)** - Guía completa de usuario (ejemplos, campos, solución de problemas)
- **[CONFIG_DEVELOPER_GUIDE.md](CONFIG_DEVELOPER_GUIDE.md)** - Guía para desarrolladores (API, arquitectura)
- **[CODE_REVIEW.md](CODE_REVIEW.md)** - Revisión completa del código
- **[FIXES_APPLIED.md](FIXES_APPLIED.md)** - Cambios y mejoras aplicadas

## 🔒 Persistencia de Datos

La configuración se guarda en **NVS (Non-Volatile Storage)**, una partición separada del firmware:

✅ **Se Mantiene:**
- Actualizaciones OTA
- Flasheo USB normal
- Reinicios y cortes de luz

❌ **Se Pierde:**
- `idf.py erase-flash` (borrado completo)
- Comando `resetConfig` (restaurar defaults)

## 🛠️ Características Técnicas

### Thread Safety
- Mutex de FreeRTOS protege acceso concurrente
- APIs thread-safe: `app_config_lock()` / `app_config_unlock()`

### Validación de Entradas
- Pines GPIO: validados (0-48)
- Puertos MQTT: validados (1-65535)
- Números UART: validados (0-2)
- Buffers: tamaños validados

### Migración Automática
- Detecta cambios de versión de configuración
- Preserva valores del usuario durante migraciones
- Log claro de proceso de migración

### Seguridad
- Contraseñas WiFi/MQTT NO se devuelven en `getConfig`
- Copias de strings con garantía de null-termination
- Buffers dimensionados para evitar overflows

## 🧪 Testing

Antes de usar en producción:

```bash
# 1. Compilar
idf.py build

# 2. Flashear (preserva NVS existente)
idf.py flash

# 3. Monitorear logs
idf.py monitor

# 4. Probar comandos MQTT
# - getConfig
# - setConfig (varios campos)
# - resetConfig

# 5. Verificar persistencia
# - Reiniciar dispositivo
# - Actualizar OTA
# - Verificar que configuración se mantiene
```

## 📊 Ejemplo Completo de Configuración

```json
{
  "action": "setConfig",
  "config": {
    // Identificación
    "deviceId": "CLUB_PADEL_TORNO_01",
    "deviceName": "Torniquete Principal - Entrada",
    
    // Funcionalidades
    "enableCards": true,
    "enableQr": true,
    "enableWifi": true,
    "enableMqtt": true,
    
    // WiFi
    "wifiSsid": "WiFi_Club",
    "wifiPass": "ClaveSegura2024",
    
    // MQTT
    "mqttHost": "mqtt.miclub.com",
    "mqttPort": 1883,
    "mqttUser": "dispositivo01",
    "mqttPass": "MqttPass2024",
    "mqttTopicRoot": "/club/torniquetes",
    
    // GPIO RC522
    "gpioRc522": {
      "mosi": 11,
      "miso": 13,
      "sck": 12,
      "ss1": 10,
      "rst1": 16,
      "ss2": 15,
      "rst2": 17
    },
    
    // GPIO Relés
    "tornInPin": 19,
    "tornOutPin": 20,
    "buzzerPin": 21,
    
    // GPIO QR
    "gpioQr": {
      "tx": 17,
      "rx": 18,
      "uartNum": 1,
      "baudRate": 9600
    }
  }
}
```

## ⚠️ Cambios que Requieren Reinicio

Estos cambios necesitan reinicio del dispositivo:
- Configuración WiFi (SSID, contraseña)
- Configuración MQTT (host, puerto, credenciales)
- Pines GPIO (cualquiera)

El sistema indica con `"needsRestart": true` en la respuesta.

## 🆘 Solución de Problemas

### Dispositivo no conecta a WiFi
```json
{"action": "getConfig"}
```
Verifica: `wifiSsid`, `wifiPass`, `enableWifi`

### Dispositivo no responde MQTT
```json
{"action": "getConfig"}
```
Verifica: `mqttHost`, `mqttPort`, `mqttUser`, `mqttPass`, `enableMqtt`

### Lectores no funcionan
```json
{"action": "getConfig"}
```
Verifica: `enableCards`, `enableQr`, pines GPIO

### Restaurar Configuración de Fábrica
```json
{"action": "resetConfig"}
```

## 📝 Valores por Defecto

Los valores por defecto están definidos en `app_config.c` función `app_config_set_defaults()`:

- **Device ID:** "SFTCLUB_DEVICE"
- **Device Name:** "Default Device"
- **WiFi SSID:** "DIGIFIBRA-3SDH"
- **MQTT Host:** "mqtt.pro.wiplaypadel.com"
- **MQTT Port:** 1883
- **Enable Cards:** false
- **Enable QR:** true
- Todos los pines GPIO con valores estándar del proyecto

Puedes modificar estos defaults en el código fuente según tus necesidades.

## 🔄 Flujo de Trabajo Recomendado

### Para Cada Dispositivo Nuevo:

1. **Flashear** firmware con configuración por defecto
2. **Conectar** y esperar arranque
3. **Configurar identificación** (deviceId, deviceName)
4. **Configurar WiFi** (si es diferente al default)
5. **Configurar MQTT** (si es diferente al default)
6. **Habilitar funcionalidades** necesarias (cards, QR)
7. **Ajustar GPIO** si el hardware usa pines diferentes
8. **Reiniciar** dispositivo
9. **Verificar** que todo funciona
10. **¡Listo!** La configuración persiste en actualizaciones

## 📧 Soporte

Para preguntas sobre:
- **Uso:** Ver `CONFIG_USER_GUIDE.md`
- **Desarrollo:** Ver `CONFIG_DEVELOPER_GUIDE.md`
- **Problemas:** Ver sección de troubleshooting en documentos

## 🏗️ Arquitectura

```
┌─────────────────────────────────────────┐
│         MQTT Commands                    │
│  (getConfig, setConfig, resetConfig)     │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      commands.c (Thread-Safe)            │
│  app_config_lock() / unlock()            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      app_config.c                        │
│  - Mutex protection                      │
│  - NVS storage                           │
│  - Validation                            │
│  - Migration                             │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      NVS Partition (Flash)               │
│  Persists across firmware updates        │
└─────────────────────────────────────────┘
```

## 🎉 Beneficios

1. **Flexibilidad:** Cada dispositivo con su configuración única
2. **Mantenimiento:** Actualizaciones OTA sin perder configuración
3. **Escalabilidad:** Fácil despliegue de múltiples dispositivos
4. **Seguridad:** Validación de entradas, thread-safe
5. **Debuggabilidad:** Logs claros, estado consultable vía MQTT
6. **Robustez:** Migración automática entre versiones

---

**¡Tu sistema de control de acceso ESP32-S3 ahora es completamente configurable y multi-dispositivo!** 🎊
