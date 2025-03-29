#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define INITIAL_ROUTE_CAPACITY 10  // Начальное количество маршрутов

void execute_command(const char *cmd) {
    printf("Выполняется: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Ошибка при выполнении: %s\n", cmd);
    }
}

// Чтение маршрутов из TOML-конфига с динамическим выделением памяти
int read_routes(const char *filename, const char *section, char ***routes) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Не удалось открыть конфигурационный файл");
        return -1;
    }

    char line[256];
    int route_count = 0;
    int capacity = INITIAL_ROUTE_CAPACITY;

    // Выделяем начальный массив указателей
    *routes = malloc(capacity * sizeof(char *));
    if (!*routes) {
        perror("Ошибка выделения памяти");
        fclose(file);
        return -1;
    }

    int in_section = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;

        if (line[0] == '[') {
            in_section = (strncmp(line + 1, section, strlen(section)) == 0);
        }

        if (in_section && strstr(line, "routes = [")) {
            char *start = strchr(line, '[');
            char *end = strchr(line, ']');
            if (start && end && end > start) {
                *end = '\0';
                start++;
                char *route = strtok(start, ", ");
                while (route) {
                    if (route_count >= capacity) {
                        // Увеличиваем размер массива в 2 раза
                        capacity *= 2;
                        *routes = realloc(*routes, capacity * sizeof(char *));
                        if (!*routes) {
                            perror("Ошибка расширения памяти");
                            fclose(file);
                            return -1;
                        }
                    }
                    (*routes)[route_count] = strdup(route);
                    if (!(*routes)[route_count]) {
                        perror("Ошибка выделения памяти для маршрута");
                        fclose(file);
                        return -1;
                    }
                    route_count++;
                    route = strtok(NULL, ", ");
                }
            }
        }
    }

    fclose(file);
    return route_count;
}

void free_routes(char **routes, int count) {
    for (int i = 0; i < count; i++) {
        free(routes[i]);
    }
    free(routes);
}

int main() {
    char **ipv4_routes, **ipv6_routes;
    int ipv4_count = read_routes("/etc/ipban/routes.toml", "ipv4_routes", &ipv4_routes);
    int ipv6_count = read_routes("/etc/ipban/routes.toml", "ipv6_routes", &ipv6_routes);

    if (ipv4_count == -1 || ipv6_count == -1) {
        return 1;
    }

    printf("Существующие маршруты перед удалением (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Существующие маршруты перед удалением (IPv6):\n");
    execute_command("ip -6 route show");

    for (int i = 0; i < ipv4_count; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ip -4 route del blackhole %s", ipv4_routes[i]);
        execute_command(cmd);
    }

    for (int i = 0; i < ipv6_count; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ip -6 route del blackhole %s", ipv6_routes[i]);
        execute_command(cmd);
    }

    printf("Маршруты после удаления (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Маршруты после удаления (IPv6):\n");
    execute_command("ip -6 route show");

    for (int i = 0; i < ipv4_count; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ip -4 route add blackhole %s", ipv4_routes[i]);
        execute_command(cmd);
    }

    for (int i = 0; i < ipv6_count; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ip -6 route add blackhole %s", ipv6_routes[i]);
        execute_command(cmd);
    }

    printf("Маршруты после добавления (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Маршруты после добавления (IPv6):\n");
    execute_command("ip -6 route show");

    free_routes(ipv4_routes, ipv4_count);
    free_routes(ipv6_routes, ipv6_count);

    return 0;
}
