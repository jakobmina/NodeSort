# NodeSort

[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![Language](https://img.shields.io/badge/language-C-blue?logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![GitHub](https://img.shields.io/badge/GitHub-jakobmina/NodeSort-blue?logo=github)](https://github.com/jakobmina/NodeSort)
[![Status](https://img.shields.io/badge/status-Active-brightgreen)](https://github.com/jakobmina/NodeSort)

## Descripción

Este trabajo presenta **NodeSort**, un algoritmo de ordenamiento basado en nodos que aprovecha el conocimiento previo del espacio de valores para eliminar comparaciones globales. En lugar de comparar elementos entre sí, el algoritmo divide el espacio conocido [mín, máx] en nodos o segmentos, y cada elemento se auto-clasifica en tiempo O(1).

## Características

- 🚀 **Distribución basada en nodos**: Aprovecha el conocimiento previo del rango de valores
- ⚡ **Clasificación en O(1)**: Cada elemento se asigna a su nodo sin comparaciones globales
- 🧵 **Paralelización**: Versiones secuencial, distribución paralela y sort paralelo
- 📊 **Benchmarking integrado**: Comparativa de rendimiento con qsort nativo

## Implementación

El repositorio contiene dos versiones principales:

### **v1: Distribución Secuencial + Sort Paralelo**
- Distribución secuencial de elementos en cubetas
- Ordenamiento paralelo de cada cubeta

### **v2: Distribución Paralela + Sort Paralelo**
- Distribución paralela: cada thread escanea su segmento
- Fusión de cubetas locales por nodo
- Ordenamiento paralelo de cubetas globales

## Compilación

```bash
gcc -pthread -o nodesort NodeSort_v2.c
./nodesort
```

## Requisitos

- Compilador C con soporte para `pthread`
- Sistema operativo POSIX (Linux, macOS, etc.)

## Licencia

Este proyecto está bajo la licencia [MIT](LICENSE).

---

**Autor**: jakobmina  
**Estado**: Activo y en desarrollo
