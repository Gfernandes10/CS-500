# Tello Core Release Install

Este guia explica como publicar e consumir o `tello_core` como artefato de
release, sem clonar o repositório completo do projeto.

## Artefato esperado

O release do `tello_core` agora publica apenas um `.tar.gz`:

```text
tello_core.tar.gz
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
O versionamento fica na tag do GitHub Release, por exemplo `v1.0.0`, e no
parametro `TELLO_CORE_VERSION` usado por consumidores como o pacote ROS.

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
source /opt/ros/jazzy/setup.bash
source "/home/gabriel_fernandes/CS 500 - ROS/install/setup.bash"

cmake -S tello_core -B build/tello_core_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DTELLO_ENABLE_ROS_CONTROL_PANEL=ON
cmake --build build/tello_core_release
cpack --config build/tello_core_release/CPackConfig.cmake
```

O `TELLO_ENABLE_ROS_CONTROL_PANEL=ON` garante que o `tello_control_panel`
incluido no artefato consuma telemetria, link quality e video por topicos ROS
quando iniciado com `--ros-mode`.

Por padrão, o `cpack --config ...` escreve o pacote no diretório em que o
comando foi executado. Se você rodar o comando a partir da raiz do workspace
`CS 500`, o arquivo será criado em:

```text
./tello_core.tar.gz
```

Esse arquivo já pode ser usado diretamente no GitHub Release.

Se preferir gerar o pacote dentro do diretório de build, use `-B`:

```bash
cpack --config build/tello_core_release/CPackConfig.cmake \
  -B build/tello_core_release
```

Nesse caso, o pacote gerado fica em:

```text
build/tello_core_release/tello_core.tar.gz
```

Publique esse arquivo na página de GitHub Releases com uma tag compatível, por
exemplo:

```text
v1.0.0
```

O nome final esperado pelo `tello_core_vendor` é:

```text
https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core.tar.gz
```

## Primeira release de teste no GitHub

Este fluxo usa a interface web do GitHub. Ele é útil quando o GitHub CLI (`gh`)
não está instalado.

### 1. Garantir que o artefato está atualizado

Na raiz do workspace `CS 500`, rode:

```bash
source /opt/ros/jazzy/setup.bash
source "/home/gabriel_fernandes/CS 500 - ROS/install/setup.bash"

cmake -S tello_core -B build/tello_core_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DTELLO_ENABLE_ROS_CONTROL_PANEL=ON

cmake --build build/tello_core_release

cpack --config build/tello_core_release/CPackConfig.cmake
```

Se você executou o `cpack` a partir da raiz `CS 500`, o arquivo será gerado em:

```text
./tello_core.tar.gz
```

Se você preferir que o arquivo seja gerado dentro do diretório de build, use:

```bash
cpack --config build/tello_core_release/CPackConfig.cmake \
  -B build/tello_core_release
```

Nesse caso, o arquivo será:

```text
build/tello_core_release/tello_core.tar.gz
```

### 2. Commit/push das mudanças do core

Antes da release, o ideal é commitar as mudanças de empacotamento/exportação:

```bash
git add tello_core/CMakeLists.txt \
  tello_core/cmake/tello_coreConfig.cmake.in \
  tello_core/apps/control_panel/main_qt.cpp \
  docs/TELLO_CORE_RELEASE_INSTALL.md

git commit -m "Package tello_core runtime release"
```

Confira a branch atual:

```bash
git branch --show-current
```

Se a branch for `main`, publique com:

```bash
git push Gabriel main
```

Se estiver em outra branch, faça push dessa branch:

```bash
git push Gabriel <nome-da-branch>
```

### 3. Criar a release no GitHub

Abra:

```text
https://github.com/Gfernandes10/CS-500/releases/new
```

Preencha:

```text
Tag: v1.0.0
Release title: tello_core v1.0.0 test release
```

Marque como **pre-release** se quiser deixar claro que é uma release de teste.

No campo de upload, anexe apenas o `.tar.gz`:

```text
tello_core.tar.gz
```

Não publique `.deb`; o fluxo atual do `tello_core_vendor` usa somente `.tar.gz`.

Depois publique a release.

### 4. Testar no workspace ROS

Depois que a release estiver publicada, rode no workspace `CS 500 - ROS`:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"

colcon build --cmake-clean-cache --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_BASE_URL=https://github.com/Gfernandes10/CS-500/releases/download
```

O vendor vai montar automaticamente:

```text
https://github.com/Gfernandes10/CS-500/releases/download/v1.0.0/tello_core.tar.gz
```

Depois:

```bash
colcon test
source install/setup.bash
ros2 launch tello_bringup control_panel.launch.py
```

### Repositório privado

Se `Gfernandes10/CS-500` estiver privado, o download direto pelo CMake pode
falhar com erro HTTP, mesmo que a release exista. Isso acontece porque
`file(DOWNLOAD ...)` baixa sem autenticação.

Para uma primeira validação local, use o escape `file://`:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"

colcon build --cmake-clean-cache --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_URL=file:///home/gabriel_fernandes/CS%20500/tello_core.tar.gz
```

Para uso em outras máquinas, as opções mais simples são:

- tornar o repositório/release acessível publicamente;
- criar um repositório público separado apenas para os artefatos de release;
- adaptar o vendor para usar autenticação por token, cuidando para não expor o
  token em cache, logs ou comandos versionados.

## Uso recomendado com tello_ros2

O workspace `tello_ros2` possui o pacote `tello_core_vendor`. Durante o
`colcon build`, ele primeiro tenta encontrar uma versão compatível já disponível
para o CMake:

```cmake
find_package(tello_core 1.0.0 CONFIG QUIET)
```

Se encontrar, ele pula o download. Se não encontrar, baixa o `.tar.gz`, extrai
o runtime dentro do próprio prefixo do workspace e permite que o `tello_driver`
use:

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

Para forçar um download/reinstall mesmo quando já existe um `tello_core`
compatível:

```bash
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_BASE_URL=https://github.com/<usuario>/<repo>/releases/download \
  -DTELLO_CORE_VENDOR_FORCE_DOWNLOAD=ON
```

Com esses parametros, o vendor monta automaticamente:

```text
${TELLO_CORE_RELEASE_BASE_URL}/v${TELLO_CORE_VERSION}/tello_core.tar.gz
```

Ou seja:

```text
https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core.tar.gz
```

## Plataformas e nome do artefato

O fluxo atual nao codifica a plataforma no nome do arquivo. O asset publicado em
cada tag deve se chamar:

```text
tello_core.tar.gz
```

Se for necessario publicar multiplas arquiteturas dentro da mesma tag no futuro,
sera preciso definir outro esquema de nomes ou reintroduzir um sufixo por
plataforma. No pacote ROS, `TELLO_CORE_PLATFORM` ficou obsoleto e e ignorado.

## Escape para teste local ou URL especial

Se quiser testar com um arquivo local, ou se o GitHub Release tiver outro layout,
use `TELLO_CORE_RELEASE_URL`. Quando esse parametro é informado, ele tem
prioridade sobre a URL montada automaticamente:

```bash
cd "/home/gabriel_fernandes/CS 500 - ROS"
colcon build --cmake-args \
  -DTELLO_CORE_VERSION=1.0.0 \
  -DTELLO_CORE_RELEASE_URL=file:///tmp/tello_core.tar.gz
```

## Hash opcional

Para builds mais reproduzíveis, publique ou calcule o SHA256:

```bash
sha256sum tello_core.tar.gz
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
  https://github.com/<usuario>/<repo>/releases/download/v1.0.0/tello_core.tar.gz
tar -xzf /tmp/tello_core.tar.gz -C $HOME/tello_core_install
```

Depois:

```bash
cd ~/ros2_ws
CMAKE_PREFIX_PATH=$HOME/tello_core_install/tello_core:$CMAKE_PREFIX_PATH colcon build
```
