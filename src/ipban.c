#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>  // For INET6_ADDRSTRLEN
#include <arpa/inet.h>   // For inet_pton and inet_ntop

// Dynamic array for storing routes
typedef struct {
    char **routes;
    int count;
    int capacity;
} RouteArray;

// Initialize route array
void init_route_array(RouteArray *array, int initial_capacity) {
    array->routes = (char**)malloc(initial_capacity * sizeof(char*));
    array->count = 0;
    array->capacity = initial_capacity;
}

// Add route to array
void add_route(RouteArray *array, const char *route) {
    // Increase array size if necessary
    if (array->count >= array->capacity) {
        array->capacity *= 2;
        array->routes = (char**)realloc(array->routes, array->capacity * sizeof(char*));
    }

    // Copy route without length limitation
    array->routes[array->count] = strdup(route);
    array->count++;
}

// Free memory
void free_route_array(RouteArray *array) {
    for (int i = 0; i < array->count; i++) {
        free(array->routes[i]);
    }
    free(array->routes);
    array->routes = NULL;
    array->count = 0;
    array->capacity = 0;
}

// Function to execute system command
void execute_command(const char *cmd) {
    printf("Executing: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Error executing command: %s\n", cmd);
    }
}

// Function to read routes from configuration file
int read_routes(const char *filename, const char *section, RouteArray *routes) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open configuration file");
        return -1;
    }

    // Use dynamic buffer to read lines of any length
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    int in_section = 0;

    while ((line_len = getline(&line, &line_cap, file)) > 0) {
        // Remove newline character
        if (line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
        }

        // Look for section start
        if (line[0] == '[') {
            char section_name[256];
            if (sscanf(line, "[%[^]]", section_name) == 1) {
                in_section = (strcmp(section_name, section) == 0);
            }
        }

        // If inside the right section, look for lines with routes
        if (in_section && strncmp(line, "routes", 6) == 0) {
            char *start = strchr(line, '[');
            char *end = strrchr(line, ']');

            if (start && end && end > start) {
                *end = '\0';
                start++;

                // Create a copy for safe work with strtok
                char *line_copy = strdup(start);

                // Remove spaces
                for (char *p = line_copy; *p; p++) {
                    if (*p == ' ') {
                        memmove(p, p + 1, strlen(p));
                        p--;
                    }
                }

                char *route = strtok(line_copy, ",");
                while (route) {
                    add_route(routes, route);
                    route = strtok(NULL, ",");
                }

                free(line_copy);
            }
        }
    }

    free(line);
    fclose(file);

    return routes->count;
}

int main() {
    RouteArray ipv4_routes, ipv6_routes;
    init_route_array(&ipv4_routes, 100);  // Initial capacity 100, will grow as needed
    init_route_array(&ipv6_routes, 100);

    // Read routes from configuration file
    int ipv4_count = read_routes("/etc/ipban/routes.toml", "ipv4_routes", &ipv4_routes);
    int ipv6_count = read_routes("/etc/ipban/routes.toml", "ipv6_routes", &ipv6_routes);

    if (ipv4_count == -1 || ipv6_count == -1) {
        free_route_array(&ipv4_routes);
        free_route_array(&ipv6_routes);
        return 1;
    }

    // Print existing routes before deletion
    printf("Existing routes before deletion (IPv4):\n");
    execute_command("ip -4 route show");
    printf("Existing routes before deletion (IPv6):\n");
    execute_command("ip -6 route show");

    // Delete existing blackhole routes (IPv4)
    for (int i = 0; i < ipv4_routes.count; i++) {
        // Use dynamic memory allocation for command
        char *cmd = (char*)malloc(strlen(ipv4_routes.routes[i]) + 64);
        sprintf(cmd, "ip -4 route del blackhole %s", ipv4_routes.routes[i]);
        execute_command(cmd);
        free(cmd);
    }

    // Delete existing blackhole routes (IPv6)
    for (int i = 0; i < ipv6_routes.count; i++) {
        char *cmd = (char*)malloc(strlen(ipv6_routes.routes[i]) + 64);
        sprintf(cmd, "ip -6 route del blackhole %s", ipv6_routes.routes[i]);
        execute_command(cmd);
        free(cmd);
    }

    // Print routes after deletion
    printf("Routes after deletion (IPv4):\n");
    execute_command("ip -4 route show");
    printf("Routes after deletion (IPv6):\n");
    execute_command("ip -6 route show");

    // Add new IPv4 Blackhole routes
    for (int i = 0; i < ipv4_routes.count; i++) {
        char *cmd = (char*)malloc(strlen(ipv4_routes.routes[i]) + 64);
        sprintf(cmd, "ip -4 route add blackhole %s", ipv4_routes.routes[i]);
        execute_command(cmd);
        free(cmd);
    }

    // Add new IPv6 Blackhole routes
    for (int i = 0; i < ipv6_routes.count; i++) {
        char *cmd = (char*)malloc(strlen(ipv6_routes.routes[i]) + 64);
        sprintf(cmd, "ip -6 route add blackhole %s", ipv6_routes.routes[i]);
        execute_command(cmd);
        free(cmd);
    }

    // Print routes after addition
    printf("Routes after addition (IPv4):\n");
    execute_command("ip -4 route show");
    printf("Routes after addition (IPv6):\n");
    execute_command("ip -6 route show");

    // Free memory
    free_route_array(&ipv4_routes);
    free_route_array(&ipv6_routes);

    return 0;
}
