# Sistema de Control de Acceso ESP32-S3

Sistema de control de acceso con soporte multidispositivo basado en ESP32-S3, con lectores RFID RC522, lector de códigos QR y control vía MQTT.

## 📋 Tabla de Contenidos

- [Características](#características)
- [Instalación de ESP-IDF](#instalación-de-esp-idf)
- [Compilación y Flasheo](#compilación-y-flasheo)
- [Configuración Multidispositivo](#configuración-multidispositivo)
- [Hardware Soportado](#hardware-soportado)
- [Estructura del Proyecto](#estructura-del-proyecto)

## 🎯 Características

- ✅ **Configuración Multidispositivo** con persistencia en NVS
- ✅ **Lectores RFID RC522** (doble lector por SPI)
- ✅ **Lector de Códigos QR** (GM861S vía UART)
- ✅ **Control Remoto** vía MQTT
- ✅ **Actualizaciones OTA** sin perder configuración
- ✅ **WiFi Configurable** por dispositivo
- ✅ **Pines GPIO Configurables**

## 🔧 Instalación de ESP-IDF

### Requisitos Previos

- Sistema operativo: Linux, macOS o Windows (WSL2 recomendado)
- Python 3.8 o superior
- Git
- Herramientas de compilación (gcc, make, cmake)

### Instalación en Linux/macOS

#### 1. Instalar Dependencias

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

**macOS:**
```bash
brew install cmake ninja dfu-util
```

#### 2. Clonar ESP-IDF

```bash
# Crear directorio para ESP-IDF
mkdir -p ~/esp
cd ~/esp

# Clonar repositorio (versión estable recomendada: v5.1 o v5.2)
git clone -b v5.1.2 --recursive https://github.com/espressif/esp-idf.git
```

#### 3. Instalar ESP-IDF

```bash
cd ~/esp/esp-idf
./install.sh esp32s3
```

Esto instalará las herramientas necesarias para ESP32-S3.

#### 4. Configurar Variables de Entorno

Cada vez que abras una nueva terminal, ejecuta:

```bash
. ~/esp/esp-idf/export.sh
```

**Opcional:** Agregar al `~/.bashrc` o `~/.zshrc` para cargar automáticamente:

```bash
echo 'alias get_idf=". $HOME/esp/esp-idf/export.sh"' >> ~/.bashrc
source ~/.bashrc
```

Ahora puedes ejecutar `get_idf` en cada sesión.

### Instalación en Windows

#### Opción 1: Usando WSL2 (Recomendado)

1. Instala WSL2 con Ubuntu:
   ```powershell
   wsl --install -d Ubuntu
   ```

2. Sigue las instrucciones de Linux dentro de WSL2

#### Opción 2: Instalador Windows

1. Descarga el instalador desde: https://dl.espressif.com/dl/esp-idf/
2. Ejecuta `esp-idf-tools-setup-x.x.x.exe`
3. Sigue el asistente de instalación
4. Usa el "ESP-IDF Command Prompt" para compilar

### Verificar Instalación

```bash
idf.py --version
```

Deberías ver la versión de ESP-IDF instalada (ej: `ESP-IDF v5.1.2`).

## 🚀 Compilación y Flasheo

### Clonar el Proyecto

```bash
cd ~/esp
git clone https://github.com/slakels/auto-esp32s3-all.git
cd auto-esp32s3-all
```

### Compilación Básica

#### 1. Configurar el Proyecto (Opcional)

```bash
idf.py menuconfig
```

Puedes ajustar:
- Configuración de particiones
- Stack sizes
- WiFi/Bluetooth settings
- etc.

#### 2. Compilar el Firmware

```bash
idf.py build
```

Esto genera los binarios en `build/`:
- `bootloader.bin`
- `partition-table.bin`
- `totpadel_controller.bin` (aplicación)

### Flasheo por USB

#### 1. Conectar el ESP32-S3

Conecta tu ESP32-S3 vía USB. Identifica el puerto:

**Linux:**
```bash
ls /dev/ttyUSB* /dev/ttyACM*
# Normalmente: /dev/ttyUSB0 o /dev/ttyACM0
```

**macOS:**
```bash
ls /dev/cu.usbserial-*
# Ejemplo: /dev/cu.usbserial-0001
```

**Windows:**
```cmd
# Ejemplo: COM3, COM4
# Ver en "Administrador de Dispositivos"
```

#### 2. Flashear Firmware Completo (Primera Vez)

```bash
# Borra todo y flashea (PIERDE CONFIGURACIÓN NVS)
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
```

⚠️ **IMPORTANTE:** `erase-flash` borra toda la configuración guardada.

#### 3. Flashear Solo Aplicación (Actualizaciones)

```bash
# Preserva la configuración en NVS
idf.py -p /dev/ttyUSB0 app-flash
```

✅ **RECOMENDADO:** Usa `app-flash` para actualizaciones, así preservas la configuración del dispositivo.

#### 4. Monitorear Logs

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Para salir: `Ctrl + ]`

#### 5. Todo en Uno (Compilar + Flashear + Monitor)

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Configuración para Múltiples Dispositivos

El firmware incluye un sistema de configuración multidispositivo. **No necesitas compilar firmware diferente para cada dispositivo**.

#### Flujo de Trabajo Recomendado:

1. **Compila el firmware una sola vez:**
   ```bash
   idf.py build
   ```

2. **Flashea todos los dispositivos con el mismo firmware:**
   ```bash
   # Dispositivo 1
   idf.py -p /dev/ttyUSB0 flash
   
   # Dispositivo 2
   idf.py -p /dev/ttyUSB1 flash
   
   # Dispositivo 3
   idf.py -p /dev/ttyUSB2 flash
   ```

3. **Configura cada dispositivo vía MQTT** (ver sección siguiente)

4. **Para futuras actualizaciones:**
   ```bash
   # Compila nueva versión
   idf.py build
   
   # Actualiza todos los dispositivos (PRESERVA SU CONFIGURACIÓN)
   idf.py -p /dev/ttyUSB0 app-flash
   idf.py -p /dev/ttyUSB1 app-flash
   idf.py -p /dev/ttyUSB2 app-flash
   ```

   O usa actualizaciones OTA remotas.

### Configuración Avanzada de Flasheo

#### Flashear Binarios Manualmente

Si ya tienes los binarios compilados:

```bash
esptool.py -p /dev/ttyUSB0 -b 460800 --before=default_reset --after=hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 build/bootloader/bootloader.bin \
  0x10000 build/totpadel_controller.bin \
  0x8000 build/partition_table/partition-table.bin
```

#### Flashear Solo Configuración (para desarrollo)

```bash
# Solo la partición NVS (requiere generar el binario NVS primero)
idf.py -p /dev/ttyUSB0 nvs-flash
```

## 🔌 Configuración Multidispositivo

### Valores por Defecto

Al flashear, cada dispositivo arranca con esta configuración:

```json
{
  "deviceId": "SFTCLUB_DEVICE",
  "deviceName": "Default Device",
  "enableCards": false,
  "enableQr": true,
  "enableWifi": true,
  "enableMqtt": true,
  "wifiSsid": "DIGIFIBRA-3SDH",
  "wifiPass": "CSFX66C2Yfyz",
  "mqttHost": "mqtt.pro.wiplaypadel.com",
  "mqttPort": 1883,
  "mqttUser": "admin",
  "mqttPass": "Abc_0123456789"
}
```

### Configurar un Dispositivo Específico

#### 1. El dispositivo se conecta con la configuración por defecto

Verás en los logs:
```
I (1234) TOTPADEL: DEVICE_ID=SFTCLUB_DEVICE
I (1235) WIFI: Using WiFi SSID=DIGIFIBRA-3SDH
I (1236) MQTT: MQTT started with broker: mqtt.pro.wiplaypadel.com
```

#### 2. Envía comando de configuración vía MQTT

Al topic: `/var/deploys/topics/SFTCLUB_DEVICE`

```json
{
  "action": "setConfig",
  "idPeticion": "config-001",
  "config": {
    "deviceId": "CLUB_TORNO_ENTRADA",
    "deviceName": "Torniquete Principal - Entrada",
    "enableCards": true,
    "enableQr": true,
    "wifiSsid": "WiFi_Club",
    "wifiPass": "ClaveSegura123",
    "tornInPin": 19,
    "tornOutPin": 20,
    "buzzerPin": 21
  }
}
```

#### 3. Reinicia el dispositivo

```bash
# Desde el monitor
Ctrl + T, Ctrl + R

# O desconecta/conecta la alimentación
```

#### 4. Verifica la nueva configuración

```json
{
  "action": "getConfig",
  "idPeticion": "verify-001"
}
```

### Ejemplo: Configurar 3 Dispositivos Diferentes

#### Dispositivo 1: Torniquete Entrada (con lectores de tarjetas)
```json
{
  "action": "setConfig",
  "config": {
    "deviceId": "CLUB_TORNO_01_ENTRADA",
    "deviceName": "Torniquete 01 - Entrada Principal",
    "enableCards": true,
    "enableQr": false,
    "gpioRc522": {
      "mosi": 11, "miso": 13, "sck": 12,
      "ss1": 10, "rst1": 16,
      "ss2": 15, "rst2": 17
    },
    "tornInPin": 19,
    "tornOutPin": 20
  }
}
```

#### Dispositivo 2: Torniquete Salida (con lector QR)
```json
{
  "action": "setConfig",
  "config": {
    "deviceId": "CLUB_TORNO_02_SALIDA",
    "deviceName": "Torniquete 02 - Salida Principal",
    "enableCards": false,
    "enableQr": true,
    "gpioQr": {
      "tx": 17, "rx": 18,
      "uartNum": 1, "baudRate": 9600
    },
    "tornInPin": 25,
    "tornOutPin": 26
  }
}
```

#### Dispositivo 3: Recepción (tarjetas y QR, pines personalizados)
```json
{
  "action": "setConfig",
  "config": {
    "deviceId": "CLUB_RECEPCION",
    "deviceName": "Torniquete Recepción",
    "enableCards": true,
    "enableQr": true,
    "gpioRc522": {
      "mosi": 23, "miso": 19, "sck": 18,
      "ss1": 5, "rst1": 22,
      "ss2": 21, "rst2": 4
    },
    "gpioQr": {
      "tx": 17, "rx": 16,
      "uartNum": 2, "baudRate": 9600
    },
    "tornInPin": 32,
    "tornOutPin": 33,
    "buzzerPin": 25
  }
}
```

### Actualizar Todos los Dispositivos (OTA)

Todos mantienen su configuración específica:

```bash
# 1. Compila nueva versión
idf.py build

# 2. Publica firmware para OTA
# (según tu sistema de OTA)

# 3. Envía comando OTA a cada dispositivo vía MQTT
{
  "action": "ota",
  "url": "https://mi-servidor.com/firmware/v2.0.0.bin"
}
```

## 📦 Hardware Soportado

### ESP32-S3

- **Chip:** ESP32-S3 (cualquier variante)
- **Flash:** 8MB o superior (ver `partitions.csv`)
- **PSRAM:** Opcional

### Periféricos

- **Lectores RFID:** 2x RC522 (SPI)
- **Lector QR:** GM861S (UART)
- **Relés:** 2x (entrada/salida)
- **Buzzer/Zumbador:** 1x
- **LED de Estado:** Integrado

### Pines por Defecto

| Componente | Pin | Función |
|------------|-----|---------|
| RC522 MOSI | GPIO 11 | SPI Bus |
| RC522 MISO | GPIO 13 | SPI Bus |
| RC522 SCK | GPIO 12 | SPI Bus |
| RC522_1 SS | GPIO 10 | Lector 1 Chip Select |
| RC522_1 RST | GPIO 16 | Lector 1 Reset |
| RC522_2 SS | GPIO 15 | Lector 2 Chip Select |
| RC522_2 RST | GPIO 17 | Lector 2 Reset |
| QR TX | GPIO 17 | UART TX |
| QR RX | GPIO 18 | UART RX |
| Relé Entrada | GPIO 19 | Control |
| Relé Salida | GPIO 20 | Control |
| Buzzer | GPIO 21 | Control |

**Nota:** Todos los pines son configurables por dispositivo vía MQTT.

## 📂 Estructura del Proyecto

```
auto-esp32s3-all/
├── CMakeLists.txt              # Configuración CMake del proyecto
├── partitions.csv              # Tabla de particiones (NVS, OTA, Storage)
├── dependencies.lock           # Dependencias gestionadas
├── main/
│   ├── CMakeLists.txt         # Build del componente main
│   ├── idf_component.yml      # Dependencias (MQTT, drivers)
│   ├── main.c                 # Punto de entrada
│   ├── app_config.c/.h        # Sistema de configuración NVS
│   ├── commands.c/.h          # Procesador de comandos MQTT
│   ├── config.c/.h            # Configuración hardcoded (valores default)
│   ├── core.c/.h              # Tipos y globals compartidos
│   ├── wifi_manager.c/.h      # Gestión WiFi
│   ├── mqtt_manager.c/.h      # Gestión MQTT
│   ├── ota_manager.c/.h       # Actualizaciones OTA
│   ├── led_status.c/.h        # LED de estado
│   ├── rc522_reader.c/.h      # Driver lectores RC522
│   └── gm861s_reader.c/.h     # Driver lector QR GM861S
├── managed_components/         # Componentes externos (MQTT, etc)
├── README.md                   # Esta guía
├── README_MULTI_DEVICE.md      # Resumen del sistema multidispositivo
├── CONFIG_USER_GUIDE.md        # Guía de usuario completa
└── CONFIG_DEVELOPER_GUIDE.md   # Guía para desarrolladores
```

## 🛠️ Comandos Útiles

### Compilación

```bash
# Compilar proyecto
idf.py build

# Limpiar build
idf.py fullclean

# Solo limpiar app (mantiene configuración)
idf.py clean
```

### Flasheo

```bash
# Flashear todo (primera vez)
idf.py -p PORT flash

# Solo aplicación (actualizaciones)
idf.py -p PORT app-flash

# Borrar flash completo
idf.py -p PORT erase-flash
```

### Monitoreo

```bash
# Monitor serial
idf.py -p PORT monitor

# Monitor con baudrate específico
idf.py -p PORT monitor -b 115200
```

### Configuración

```bash
# Menú de configuración
idf.py menuconfig

# Ver configuración actual
idf.py show_efuse_table
```

### Información

```bash
# Ver tamaño de binarios
idf.py size

# Ver tamaño por componente
idf.py size-components

# Ver tamaño por archivo
idf.py size-files
```

## 🐛 Solución de Problemas

### Error: "Puerto no encontrado"

```bash
# Linux: Agregar usuario al grupo dialout
sudo usermod -a -G dialout $USER
# Cerrar sesión y volver a entrar

# Verificar permisos
ls -l /dev/ttyUSB0
```

### Error: "No se puede conectar al ESP32"

1. Asegúrate de que el ESP32-S3 esté en modo de bootloader:
   - Mantén presionado el botón BOOT
   - Presiona y suelta el botón RESET
   - Suelta el botón BOOT

2. Intenta con velocidad más baja:
   ```bash
   idf.py -p PORT -b 115200 flash
   ```

### Error de Compilación: "Component not found"

```bash
# Actualizar componentes gestionados
idf.py reconfigure
idf.py build
```

### El dispositivo no se conecta a WiFi

1. Verifica la configuración:
   ```json
   {"action": "getConfig"}
   ```

2. Verifica que `enableWifi` sea `true`

3. Verifica SSID y contraseña

4. Restaura defaults si es necesario:
   ```json
   {"action": "resetConfig"}
   ```

### La configuración se pierde al actualizar

⚠️ Asegúrate de usar `app-flash` en lugar de `flash` completo:

```bash
# CORRECTO (preserva NVS)
idf.py -p PORT app-flash

# INCORRECTO (borra NVS)
idf.py -p PORT erase-flash
idf.py -p PORT flash
```

## 📚 Documentación Adicional

- **[README_MULTI_DEVICE.md](README_MULTI_DEVICE.md)** - Resumen del sistema multidispositivo
- **[CONFIG_USER_GUIDE.md](CONFIG_USER_GUIDE.md)** - Guía completa de configuración
- **[CONFIG_DEVELOPER_GUIDE.md](CONFIG_DEVELOPER_GUIDE.md)** - Guía para desarrolladores
- **[ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)** - Documentación oficial de ESP-IDF

## 🤝 Contribuir

Para contribuir al proyecto:

1. Fork el repositorio
2. Crea una rama para tu feature (`git checkout -b feature/nueva-funcionalidad`)
3. Commit tus cambios (`git commit -am 'Añadir nueva funcionalidad'`)
4. Push a la rama (`git push origin feature/nueva-funcionalidad`)
5. Crea un Pull Request

## 📄 Licencia

[Especifica tu licencia aquí]

## 🆘 Soporte

Para preguntas o problemas:
- Abre un Issue en GitHub
- Consulta la documentación en `/docs`
- Revisa los logs del dispositivo con `idf.py monitor`

---

**¡Tu sistema de control de acceso ESP32-S3 multidispositivo está listo para usar!** 🎉
