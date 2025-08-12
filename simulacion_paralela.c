// simulacion_trafico_omp.c
// Mini proyecto OpenMP: Semáforos + Vehículos con ajuste dinámico de hilos
// Cumple pasos 1–7 del PDF (estructuras, inicialización, comportamiento, simulación, hilos dinámicos).

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

// -------------------- Paso 1: Estructuras --------------------
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
} Interseccion; // (conceptual; aquí es una pista 1D con semáforos)

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

// -------------------- Paso 2: Inicialización --------------------
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

// -------------------- Paso 3: Semáforos (lógica y actualización) --------------------
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
// Para robustez, consideramos un "stop range" = 0 (la celda del semáforo).
// Nota: usamos snapshot de semáforos para evitar leer mientras se actualizan.
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

// -------------------- Paso 5: Bucle de simulación --------------------
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
        // 1) Actualizar semáforos
        actualizar_semaforos(s, n_sem);

        // 2) Snapshot de semáforos para que el movimiento lea un estado estable
        Semaforo *snap = (Semaforo*)malloc(sizeof(Semaforo) * n_sem);
        memcpy(snap, s, sizeof(Semaforo) * n_sem);

        // 3) Mover vehículos
        mover_vehiculos(v, n_veh, snap, n_sem, road_len);

        free(snap);

        // 4) Mostrar estado
        imprimir_estado(v, n_veh, s, n_sem, i);

        if (delay_seg > 0) SLEEP_SEC(delay_seg);
    }
}

// -------------------- Paso 6: Ajuste dinámico de hilos --------------------
void simular_dinamico(int iteraciones, Vehiculo *v, int n_veh, Semaforo *s, int n_sem, int road_len, int delay_seg, int usar_secciones) {
    omp_set_dynamic(1); // permitir ajuste dinámico
    for (int i = 0; i < iteraciones; i++) {
        // Heurística: 1 hilo por 8 vehículos + 1 por cada 4 semáforos, mínimo 2
        int hilos = (n_veh + 7) / 8 + (n_sem + 3) / 4;
        if (hilos < 2) hilos = 2;
        omp_set_num_threads(hilos);

        if (usar_secciones) {
            // Snapshot previo para que la sección de "mover" lea un estado consistente
            Semaforo *snap = (Semaforo*)malloc(sizeof(Semaforo) * n_sem);
            memcpy(snap, s, sizeof(Semaforo) * n_sem);

            #pragma omp parallel sections
            {
                #pragma omp section
                {
                    actualizar_semaforos(s, n_sem);
                }
                #pragma omp section
                {
                    mover_vehiculos(v, n_veh, snap, n_sem, road_len);
                }
            }
            free(snap);
        } else {
            // Secuencial por iteración (pero cada tarea interna está paralelizada)
            actualizar_semaforos(s, n_sem);

            Semaforo *snap = (Semaforo*)malloc(sizeof(Semaforo) * n_sem);
            memcpy(snap, s, sizeof(Semaforo) * n_sem);

            mover_vehiculos(v, n_veh, snap, n_sem, road_len);
            free(snap);
        }

        imprimir_estado(v, n_veh, s, n_sem, i);
        if (delay_seg > 0) SLEEP_SEC(delay_seg);
    }
}

// -------------------- Paso 7: Main, pruebas y opciones --------------------
static void uso(const char *prog) {
    fprintf(stderr,
        "Uso: %s <vehiculos> <semaforos> <iteraciones> <largo_carretera> [delay_seg=0] [ciclo_semaforo=9] [usar_secciones=1] [seed]\n"
        "Ej:  %s 20 4 5 100 0 9 1 42\n",
        prog, prog
    );
}

int main(int argc, char **argv) {
    if (argc < 5) {
        uso(argv[0]);
        return 1;
    }
    int n_veh  = atoi(argv[1]);
    int n_sem  = atoi(argv[2]);
    int iters  = atoi(argv[3]);
    int road   = atoi(argv[4]);
    int delay  = (argc > 5) ? atoi(argv[5]) : 0;
    int ciclo  = (argc > 6) ? atoi(argv[6]) : 9; // verde 50%, amarillo 20%, rojo resto
    int usar_secciones = (argc > 7) ? atoi(argv[7]) : 1;
    unsigned int seed  = (argc > 8) ? (unsigned int)strtoul(argv[8], NULL, 10) : (unsigned int)time(NULL);

    if (n_veh <= 0 || n_sem <= 0 || iters <= 0 || road <= 5) {
        uso(argv[0]);
        return 1;
    }

    Vehiculo *veh = (Vehiculo*)malloc(sizeof(Vehiculo) * n_veh);
    Semaforo *sem = (Semaforo*)malloc(sizeof(Semaforo) * n_sem);

    inicializar_vehiculos(veh, n_veh, road, seed);
    inicializar_semaforos(sem, n_sem, road, ciclo);

    printf("Simulación de tráfico con OpenMP\n");
    printf("Vehículos: %d | Semáforos: %d | Iteraciones: %d | Largo: %d | Hilos dinámicos ON\n",
           n_veh, n_sem, iters, road);
    printf("Secciones paralelas: %s | Delay: %d s | Ciclo semáforo: %d ticks\n",
           usar_secciones ? "Sí" : "No", delay, ciclo);

    // Ejecuta la versión dinámica (paso 6). La simple queda disponible si la prefieres.
    simular_dinamico(iters, veh, n_veh, sem, n_sem, road, delay, usar_secciones);

    free(veh);
    free(sem);
    return 0;
}
