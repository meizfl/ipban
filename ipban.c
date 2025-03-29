#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>  // Для INET6_ADDRSTRLEN
#include <arpa/inet.h>   // Для inet_pton и inet_ntop

#define MAX_ROUTES 100
#define ROUTE_CMD_SIZE 256

// Функция для выполнения системной команды
void execute_command(const char *cmd) {
    printf("Выполняется: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Ошибка при выполнении команды: %s\n", cmd);
    }
}

// Функция для чтения маршрутов из конфигурационного файла
int read_routes(const char *filename, const char *section, char routes[MAX_ROUTES][INET6_ADDRSTRLEN]) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Не удалось открыть конфигурационный файл");
        return -1;
    }

    char line[256];
    int route_count = 0;
    int in_section = 0;

    while (fgets(line, sizeof(line), file)) {
        // Убираем символ новой строки
        line[strcspn(line, "\n")] = 0;

        // Ищем начало секции
        if (line[0] == '[') {
            in_section = (strncmp(line + 1, section, strlen(section)) == 0);
        }

        // Если внутри нужной секции, ищем строки с маршрутами
        if (in_section && strncmp(line, "routes", 6) == 0) {
            char *start = strchr(line, '[');
            char *end = strchr(line, ']');
            if (start && end && end > start) {
                *end = '\0';
                start++;
                char *route = strtok(start, ",");
                while (route) {
                    if (route_count < MAX_ROUTES) {
                        strncpy(routes[route_count], route, INET6_ADDRSTRLEN);
                        route_count++;
                    }
                    route = strtok(NULL, ",");
                }
            }
        }
    }

    fclose(file);
    return route_count;
}

int main() {
    char ipv4_routes[MAX_ROUTES][INET6_ADDRSTRLEN];
    char ipv6_routes[MAX_ROUTES][INET6_ADDRSTRLEN];

    // Чтение маршрутов из конфигурационного файла
    int ipv4_count = read_routes("/etc/ipban/routes.toml", "ipv4_routes", ipv4_routes);
    int ipv6_count = read_routes("/etc/ipban/routes.toml", "ipv6_routes", ipv6_routes);

    if (ipv4_count == -1 || ipv6_count == -1) {
        return 1;
    }

    // Печать существующих маршрутов перед удалением
    printf("Существующие маршруты перед удалением (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Существующие маршруты перед удалением (IPv6):\n");
    execute_command("ip -6 route show");

    // Удаление существующих blackhole маршрутов (IPv4)
    for (int i = 0; i < ipv4_count; i++) {
        char cmd[ROUTE_CMD_SIZE];
        snprintf(cmd, sizeof(cmd), "ip -4 route del blackhole %s", ipv4_routes[i]);
        execute_command(cmd);
    }

    // Удаление существующих blackhole маршрутов (IPv6)
    for (int i = 0; i < ipv6_count; i++) {
        char cmd[ROUTE_CMD_SIZE];
        snprintf(cmd, sizeof(cmd), "ip -6 route del blackhole %s", ipv6_routes[i]);
        execute_command(cmd);
    }

    // Печать маршрутов после удаления
    printf("Маршруты после удаления (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Маршруты после удаления (IPv6):\n");
    execute_command("ip -6 route show");

    // Добавление новых IPv4 Blackhole маршрутов
    for (int i = 0; i < ipv4_count; i++) {
        char cmd[ROUTE_CMD_SIZE];
        snprintf(cmd, sizeof(cmd), "ip -4 route add blackhole %s", ipv4_routes[i]);
        execute_command(cmd);
    }

    // Добавление новых IPv6 Blackhole маршрутов
    for (int i = 0; i < ipv6_count; i++) {
        char cmd[ROUTE_CMD_SIZE];
        snprintf(cmd, sizeof(cmd), "ip -6 route add blackhole %s", ipv6_routes[i]);
        execute_command(cmd);
    }

    // Печать маршрутов после добавления
    printf("Маршруты после добавления (IPv4):\n");
    execute_command("ip -4 route show");

    printf("Маршруты после добавления (IPv6):\n");
    execute_command("ip -6 route show");

    return 0;
}
