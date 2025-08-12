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
// -------------------- Inicialización --------------------
void inicializar_vehiculos(Vehiculo *v, int n, int road_len, unsigned int seed) {
    // Distribuir vehículos de forma pseudoaleatoria no superpuesta y velocidad 1–2
    srand(seed);
    // Para evitar muchas colisiones iniciales, ubicamos espaciados con jitter
    int espacio = (road_len > n) ? (road_len / n) : 1;
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        int jitter = (espacio > 1) ? (rand() % espacio) : 0;
        v[i].id = i;
        v[i].pos = mod_pos(i * espacio + jitter, road_len);
        v[i].vel_max = 1 + (rand() % 2); // 1 o 2
    }
}

void inicializar_semaforos(Semaforo *s, int n, int road_len, int ciclo_total) {
    // Colocar semáforos espaciados a lo largo de la carretera.
    // Ajuste de ciclo: verde > amarillo > rojo
    int espacio = (road_len > n) ? (road_len / n) : 1;
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        s[i].id = i;
        s[i].pos = mod_pos(i * espacio, road_len);
        s[i].estado = (i % 2 == 0) ? VERDE : ROJO; // alternar para variedad
        s[i].t_en_estado = 0;

        // Duraciones proporcionales al ciclo total
        s[i].dur_verde    = (int)(ciclo_total * 0.5); // 50%
        s[i].dur_amarillo = (int)(ciclo_total * 0.2); // 20%
        s[i].dur_rojo     = ciclo_total - s[i].dur_verde - s[i].dur_amarillo;
        if (s[i].dur_verde < 1) s[i].dur_verde = 1;
        if (s[i].dur_amarillo < 1) s[i].dur_amarillo = 1;
        if (s[i].dur_rojo < 1) s[i].dur_rojo = 1;
    }
}

// -------------------- Semáforos --------------------
static inline EstadoSemaforo siguiente_estado(const Semaforo *s) {
    switch (s->estado) {
        case VERDE:    return AMARILLO;
        case AMARILLO: return ROJO;
        case ROJO:     return VERDE;
        default:       return ROJO;
    }
}

void actualizar_semaforos(Semaforo *s, int n) {
    // Paralelizar por semáforo
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        s[i].t_en_estado++;
        int limite = 0;
        switch (s[i].estado) {
            case VERDE:    limite = s[i].dur_verde;    break;
            case AMARILLO: limite = s[i].dur_amarillo; break;
            case ROJO:     limite = s[i].dur_rojo;     break;
            default:       limite = 1;                 break;
        }
        if (s[i].t_en_estado >= limite) {
            s[i].estado = siguiente_estado(&s[i]);
            s[i].t_en_estado = 0;
        }
    }
}
// -------------------- Paso 4: Vehículos (lógica de movimiento) --------------------
// Regla simple: un vehículo avanza hasta su vel_max salvo que el siguiente semáforo
// por delante esté en ROJO o AMARILLO exactamente en su posición destino (o antes).
// Para robustez un "stop range" = 0 (la celda del semáforo).
// Btw usamos snapshot de semáforos para evitar leer mientras se actualizan.
void mover_vehiculos(Vehiculo *v, int n_veh, const Semaforo *sem_snapshot, int n_sem, int road_len) {
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n_veh; i++) {
        int pos_actual = v[i].pos;
        int paso = v[i].vel_max;
        int puede_mover = 1;

        // Calcular posición destino tentativa
        int destino = mod_pos(pos_actual + paso, road_len);

        // Verificar semáforo en destino
        for (int j = 0; j < n_sem; j++) {
            // Si hay semáforo justo en la celda de destino y no está VERDE, detente
            if (sem_snapshot[j].pos == destino) {
                if (sem_snapshot[j].estado == ROJO || sem_snapshot[j].estado == AMARILLO) {
                    puede_mover = 0;
                }
                break;
            }
        }

        if (puede_mover) {
            v[i].pos = destino;
        } // si no puede, se queda en su lugar
    }
}
// -------------------- Bucle de simulación --------------------
void imprimir_estado(const Vehiculo *v, int n_veh, const Semaforo *s, int n_sem, int iter) {
    printf("\nIteración %d\n", iter + 1);
    for (int i = 0; i < n_veh; i++) {
        printf("Vehículo %2d - Posición: %d\n", v[i].id, v[i].pos);
    }
    for (int j = 0; j < n_sem; j++) {
        printf("Semáforo %d - Estado: %s\n", s[j].id, estado_to_str(s[j].estado));
    }
}

void simular_simple(int iteraciones, Vehiculo *v, int n_veh, Semaforo *s, int n_sem, int road_len, int delay_seg) {
    for (int i = 0; i < iteraciones; i++) {
        // Actualizar semáforos
        actualizar_semaforos(s, n_sem);

        // Snapshot de semáforos para que el movimiento lea un estado estable
        Semaforo *snap = (Semaforo*)malloc(sizeof(Semaforo) * n_sem);
        memcpy(snap, s, sizeof(Semaforo) * n_sem);

        // Mover vehículos
        mover_vehiculos(v, n_veh, snap, n_sem, road_len);

        free(snap);

        // Mostrar estado
        imprimir_estado(v, n_veh, s, n_sem, i);

        if (delay_seg > 0) SLEEP_SEC(delay_seg);
    }
}
