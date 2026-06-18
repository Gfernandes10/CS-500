# Explicação: CMakeLists.txt - Fundamentos

**Data:** 2026-06-15  
**Fase:** Phase A - Foundation

---

## Contexto da Dúvida

Durante a Phase A - Foundation, foi necessário criar o arquivo raiz `CMakeLists.txt` para configurar o build do projeto `tello_core`. O usuário questionou o que é CMakeLists.txt e o que foi definido dentro dele.

---

## A Dúvida

> "Antes de prosseguir para a próxima etapa me explica o que é o CMakeList e o que foi definido dentro dele."

---

## A Resposta

### O que é CMakeLists.txt?

`CMakeLists.txt` é um **arquivo de configuração de build** que diz ao **CMake** (uma ferramenta de compilação) como organizar, compilar e linkar seu projeto C/C++.

**Por que usar CMake em vez de compilar manualmente?**
- **Automação:** não precisa lembrar de flags de compilação.
- **Portabilidade:** funciona em Linux, macOS, Windows sem alterações.
- **Escalabilidade:** gerencia projetos grandes com múltiplos arquivos e dependências.
- **Simplicidade:** abstrai detalhes específicos do compilador (gcc, clang, MSVC, etc.).

---

### O que foi definido no nosso CMakeLists.txt?

#### 1. Versão mínima e identificação do projeto
```cmake
cmake_minimum_required(VERSION 3.16)
project(tello_core VERSION 1.0.0 LANGUAGES CXX)
```
- Exige CMake 3.16+ (compatibilidade).
- Define projeto como `tello_core`, versão 1.0.0, usando C++.

#### 2. Padrão de linguagem
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```
- Força **C++17** (moderno, suporta `std::thread`, `std::optional`, structured bindings).
- `REQUIRED ON` garante que se o compilador não suportar C++17, o build falha (ao invés de silenciosamente usar versão anterior).

#### 3. Flags de compilação
```cmake
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
```
- **Debug:** `-g` (mantém símbolos para debugging), `-O0` (sem otimização, compilação mais rápida).
- **Release:** `-O3` (otimização agressiva, executável mais rápido).

#### 4. Inclusão de configurações extras
```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake)
```
- Carrega arquivo adicional com warnings e flags extras (vamos criar na próxima etapa).

#### 5. Definição da biblioteca principal
```cmake
add_library(tello_core
    src/logger.cpp
    src/udp_socket.cpp
    src/command_executor.cpp
    src/tello_client.cpp
    src/state_parser.cpp
    src/state_receiver.cpp
    src/video_receiver.cpp
    src/metrics.cpp
)

target_include_directories(tello_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```
- Compila todos os arquivos `.cpp` em uma **biblioteca reutilizável** chamada `tello_core`.
- Define diretório de headers públicos (`include/`), acessível para código que usa a biblioteca.

#### 6. Definição do executável (CLI app)
```cmake
add_executable(tello_cli
    apps/cli/main.cpp
)

target_link_libraries(tello_cli PRIVATE tello_core)
```
- Cria executável `tello_cli` que compila `apps/cli/main.cpp`.
- **Linka** (conecta) com a biblioteca `tello_core`.

#### 7. Suporte a testes
```cmake
enable_testing()
```
- Habilita sistema de testes do CMake (vamos adicionar testes nas próximas fases).

---

### Diagrama de dependências

```
┌─────────────────────────────────────────┐
│      tello_core (LIBRARY)               │
├─────────────────────────────────────────┤
│ - logger.cpp                            │
│ - udp_socket.cpp                        │
│ - command_executor.cpp                  │
│ - tello_client.cpp                      │
│ - state_parser.cpp                      │
│ - state_receiver.cpp                    │
│ - video_receiver.cpp                    │
│ - metrics.cpp                           │
└─────────────────────────────────────────┘
                    ↑
                    │ LINKS TO
                    │
┌─────────────────────────────────────────┐
│   tello_cli (EXECUTABLE)                │
├─────────────────────────────────────────┤
│ - apps/cli/main.cpp                     │
└─────────────────────────────────────────┘
```

---

### Fluxo de compilação

Quando você executa `cmake && make`:

1. **CMake lê** `CMakeLists.txt`.
2. **Gera** `Makefile` (instruções específicas do SO).
3. **Make compila** cada `.cpp` → arquivos objeto (`.o`).
4. **Arruma** objetos em biblioteca `libtello_core.a` (estática, Linux).
5. **Linka** `tello_cli` com `libtello_core.a`.

---

### Termos-chave

| Termo | Significado |
|-------|-------------|
| **add_library** | Cria uma biblioteca (.a, .so, .lib) a partir de arquivos-fonte |
| **add_executable** | Cria um programa executável a partir de arquivos-fonte |
| **target_link_libraries** | Conecta um alvo (lib/exe) com outras bibliotecas |
| **target_include_directories** | Define onde o compilador procura por headers |
| **set()** | Define variável CMake |
| **include()** | Carrega outro arquivo CMake |

---

## Pontos-chave para lembrar

✅ CMakeLists.txt é o **"receita"** de como compilar o projeto.  
✅ **Não é** código C++, é declaração de build.  
✅ Múltiplos CMakeLists.txt podem existir em subpastas (vamos adicionar em `tests/`, `apps/`, etc.).  
✅ É a base para integração contínua (CI/CD) e packaging.

---

## Próximos passos

- Criar `cmake/CompilerOptions.cmake` com flags de warning adicionais.
- Criar headers com contratos (`.hpp`).
- Criar stubs de implementação (`.cpp`).
- Validar compilação.

