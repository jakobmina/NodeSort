---
id: nodesort
type: goal-and-requirements
title: nodesort
status: active
---

# nodesort

Biblioteca C portable para ordenar en paralelo arrays numéricos (enteros y flotantes de 32 bits),
más rápido que `qsort`.

## Overview

nodesort nace de un prototipo de *bucket sort* paralelo (reparto en "nodos"/cubetas + ordenación por
hilo) que solo existía como un `main` de benchmark. El producto es ese algoritmo convertido en una
biblioteca instalable: API C estable, sin fallos en casos límite, multiplataforma (Linux, macOS,
Windows con MSVC y MinGW) y distribuida como paquete. Reemplaza al prototipo: sus variantes v1/v2 no
se conservan.

Se usa como **módulo dentro de sistemas más complejos**, incluido software de computación cuántica
(QC). La corrección de errores de esos sistemas (códigos de Hamming y de Steane de 7 qubits) es del
sistema anfitrión, no de nodesort (unconfirmed: reparto de responsabilidades). Por eso la biblioteca
se comporta como un componente embebible: sin estado global, sin imprimir, sin abortar el proceso.

## Problem Statement

Quien necesita ordenar millones de enteros en C tiene `qsort` (secuencial y lento por la llamada al
comparador) o bibliotecas C++ (`std::sort` paralelo, ips4o) que no se pueden usar desde C puro ni
integrar con un `find_package`/`pkg-config`. El prototipo demostraba la ganancia, pero fallaba con
`n = 0`, con todos los valores iguales o con rangos que desbordan `int`, y no comprobaba errores de
memoria ni de hilos: no se podía confiar en él ni distribuirlo.

## Target Users

- **Desarrolladores C/C++** que integran la biblioteca en su proyecto vía CMake, pkg-config o paquete
  del sistema.
- **Empaquetadores** que la construyen e instalan en Linux (`.deb`/`.rpm`), macOS y Windows.

## Jobs to Be Done

- Cuando tengo un array grande de `int32_t`/`uint32_t`, quiero ordenarlo con una sola llamada que use
  todos los núcleos, para no escribir ni mantener mi propio sort paralelo.
- Cuando tengo puntuaciones `float` (p. ej. similitudes de embeddings), quiero ordenarlas — también
  de mayor a menor — sin que un NaN aparezca entre las primeras posiciones.
- Cuando integro una dependencia, quiero que se instale y enlace con las herramientas estándar en
  cualquier SO, para no parchear su build.

## Goals

- Resultado idéntico a `qsort` para toda entrada, incluidos casos límite (vacío, un elemento, todos
  iguales, `INT32_MIN`/`INT32_MAX`, datos sesgados).
- `float`: orden numérico con `-0.0` antes que `+0.0`; los NaN siempre al final, en ascendente y en
  descendente, en su orden de entrada.
- Más rápido que `qsort` en arrays grandes (≥ 1M elementos) en máquinas multinúcleo.
- Nunca aborta ni corrompe memoria: todo fallo (memoria, hilos, argumentos) vuelve como código de
  error y deja el array exactamente como estaba.
- Embebible: reentrante, sin estado global, sin salida por consola; llamadas concurrentes desde
  distintos hilos del anfitrión sobre arrays distintos son seguras.
- Compila sin avisos y pasa los tests en Linux, macOS y Windows (MSVC y MinGW).

## Non-Goals

- Comparador genérico definido por el usuario.
- Mantener las variantes v1/v2 del prototipo.

## Success Conditions

- Los tests (casos límite + propiedad contra `qsort`) pasan en CI en las tres plataformas, también
  con AddressSanitizer/UndefinedBehaviorSanitizer y ThreadSanitizer donde existan.
- El benchmark incluido muestra speedup frente a `qsort` con ≥ 1M elementos y ≥ 2 hilos.
- Un proyecto externo consume la biblioteca instalada con `find_package(nodesort)` y con
  `pkg-config nodesort`.

## Capabilities

- **Ordenación en el sitio** de `int32_t`, `uint32_t` y `float`, paralela, con número de hilos
  automático (núcleos disponibles) o fijado por el usuario.
- **Orden ascendente o descendente**, a elección del usuario, para todos los tipos.
- **Opciones** de hilos, dirección y umbral bajo el cual se ordena en secuencial (entradas
  pequeñas); un binario compilado con una versión anterior de la cabecera sigue funcionando.
- **Errores explícitos** por código de retorno, con texto legible.
- **Benchmark CLI** reproducible (tamaño, distribución, hilos, repeticiones, salida CSV) que compara
  contra `qsort`.
- **Distribución**: biblioteca estática y compartida, instalación con CMake (`find_package`) y
  pkg-config, paquetes `.deb`/`.rpm`, releases versionados (SemVer) con licencia MIT.

## Non-Functional Requirements

- C11, sin dependencias externas salvo la API de hilos del SO (pthreads / Win32).
- API y ABI pública mínima: solo se exportan los símbolos de la cabecera pública.

## Alternatives Considered

| Proyecto | Qué le falta frente a nodesort |
|---|---|
| [IPS⁴o](https://github.com/ips4o/ips4o) / [IPS²Ra](https://github.com/ips4o/ips2ra) | C++17 header-only; no se puede usar desde C puro. |
| [pdqsort](https://github.com/orlp/pdqsort) | C++ y secuencial. |
| [RADULS2](https://arxiv.org/abs/1703.00687) | C++, orientado a registros grandes en Linux. |
| [HXTSort](https://www.hextreme.eu/hxtsort/) | Macros de un solo header atadas a OpenMP; sin paquete ni API de errores. |
| [libpsort](https://github.com/ocxtal/libpsort) | Solo pthreads (sin Windows/MSVC), sin mantenimiento desde 2016. |
| `qsort` | Secuencial y lento por la llamada indirecta al comparador. |

## Technology

| Aspect | Choice | Rationale |
|---|---|---|
| Lenguaje | C11 | Portable; MSVC lo soporta desde VS2019 (sin `threads.h`, por eso hilos propios). |
| Hilos | Capa interna pthreads / Win32 | Un solo código para MSVC, MinGW, Linux y macOS. |
| Build | CMake ≥ 3.16 + CPack | Estándar de facto; da `find_package`, pkg-config y `.deb`/`.rpm`. |
| CI | GitHub Actions | Matriz Linux/macOS/Windows (MSVC + MinGW) con sanitizers. |
| Licencia | MIT | Mínima fricción de adopción y empaquetado. |
