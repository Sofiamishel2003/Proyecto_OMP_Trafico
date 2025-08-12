#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
  #define SLEEP_SEC(x) Sleep((x) * 1000)
#else
  #include <unistd.h>
  #define SLEEP_SEC(x) sleep(x)
#endif

// -------------------- Estructuras --------------------
typedef enum {
    ROJO = 0,
    VERDE = 1,
    AMARILLO = 2
} EstadoSemaforo;

typedef struct {
    int id;
    int pos;            // posición sobre una carretera 1D [0, road_len)
    EstadoSemaforo estado;
    int t_en_estado;    // tiempo transcurrido en el estado actual (ticks)
    // Duraciones configurables por estado:
    int dur_rojo;
    int dur_verde;
    int dur_amarillo;
} Semaforo;

typedef struct {
    int id;
    int pos;        // posición actual
    int vel_max;    // velocidad máxima (celdas por tick)
} Vehiculo;

typedef struct {
    int largo;          // largo de la carretera (bucle 1D)
    Vehiculo *vehiculos;
    int n_veh;
    Semaforo *semaforos;
    int n_sem;
} Interseccion; 

// -------------------- Utilidades --------------------
static inline int mod_pos(int x, int m) {
    int r = x % m;
    return (r < 0) ? r + m : r;
}

static inline const char* estado_to_str(EstadoSemaforo e) {
    switch (e) {
        case ROJO:     return "0";
        case VERDE:    return "1";
        case AMARILLO: return "2";
        default:       return "?";
    }
}

