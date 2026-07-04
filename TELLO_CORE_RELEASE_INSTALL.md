# Tello Core Release Install

Este guia explica como publicar e consumir o `tello_core` como artefato de
release, sem clonar o repositório completo do projeto.

## Artefato esperado

O release do `tello_core` agora publica apenas um `.tar.gz`:

```text
tello_core-<versao>-Linux-x86_64.tar.gz
```

Esse arquivo contem somente o runtime necessario para consumidores como o
`tello_ros2`:

```text
bin/tello_cli
bin/tello_control_panel
include/tello/...
lib/libtello_core.a
lib/cmake/tello_core/...
share/tello_core/control_profiles.json
```

Ele nao inclui `docs/`, `notebooks/`, `results/` ou arquivos de pesquisa.

## Dependencias do sistema

Em Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  qtbase5-dev
```

Opcional:

```bash
sudo apt install -y libopencv-dev
```

## Gerar o artefato de release

No computador que vai publicar o release:

```bash
cmake -S tello_core -B build/tello_core_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/tello_core_release
cpack --config build/tello_core_release/CPackConfig.cmake
```

O pacote gerado fica em:

```text
build/tello_core_release/tello_core-1.0.0-Linux-x86_64.tar.gz
```

Publique esse arquivo na página de GitHub Releases com uma tag compatível, por
exemplo:

```text
v1.0.0
```

O nome final esperado pelo `tello_core_vendor` é:

```text
https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core-1.0.0-Linux-x86_64.tar.gz
```

## Uso recomendado com tello_ros2

O workspace `tello_ros2` possui o pacote `tello_core_vendor`. Durante o
`colcon build`, ele baixa o `.tar.gz`, extrai o runtime dentro do próprio
prefixo do workspace e permite que o `tello_driver` use:

```cmake
find_package(tello_core REQUIRED)
target_link_libraries(tello_driver_node tello_core::tello_core)
```

Build recomendado:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_BASE_URL=https://github.com/<usuario>/<repo>/releases/download
```

Com esses parametros, o vendor monta automaticamente:

```text
${TELLO_CORE_RELEASE_BASE_URL}/v${TELLO_CORE_VERSION}/tello_core-${TELLO_CORE_VERSION}-Linux-x86_64.tar.gz
```

Ou seja:

```text
https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core-1.0.0-Linux-x86_64.tar.gz
```

## Configurar outra plataforma

O sufixo padrao vem de `CMAKE_SYSTEM_NAME-CMAKE_SYSTEM_PROCESSOR`, normalmente:

```text
Linux-x86_64
```

Se o artefato tiver outro sufixo:

```bash
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_BASE_URL=https://github.com/<usuario>/<repo>/releases/download \
  -DTELLO_CORE_PLATFORM=Linux-aarch64
```

## Escape para teste local ou URL especial

Se quiser testar com um arquivo local, ou se o GitHub Release tiver outro layout,
use `TELLO_CORE_RELEASE_URL`. Quando esse parametro é informado, ele tem
prioridade sobre a URL montada automaticamente:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_URL=file:///tmp/tello_core-1.0.0-Linux-x86_64.tar.gz
```

## Hash opcional

Para builds mais reproduzíveis, publique ou calcule o SHA256:

```bash
sha256sum tello_core-1.0.0-Linux-x86_64.tar.gz
```

E passe o hash no build:

```bash
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_BASE_URL=https://github.com/<usuario>/<repo>/releases/download \
  -DTELLO_CORE_URL_HASH=SHA256=<sha256>
```

## Instalação manual sem vendor

O fluxo recomendado é usar `tello_core_vendor`, mas também é possível instalar o
`.tar.gz` manualmente:

```bash
mkdir -p $HOME/tello_core_install
wget -O /tmp/tello_core.tar.gz \
  https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core-1.0.0-Linux-x86_64.tar.gz
tar -xzf /tmp/tello_core.tar.gz -C $HOME/tello_core_install
```

Depois:

```bash
cd ~/ros2_ws
CMAKE_PREFIX_PATH=$HOME/tello_core_install:$CMAKE_PREFIX_PATH colcon build
```

