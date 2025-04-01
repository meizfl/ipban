#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>       // For isspace
#include <netinet/in.h>  // For INET6_ADDRSTRLEN
#include <arpa/inet.h>   // For inet_pton and inet_ntop
#include <unistd.h>      // For popen/pclose
#include <sys/wait.h>    // For WIFEXITED, WEXITSTATUS

#define BGPQ_COMMAND "bgpq4" // Or "bgpq3" if you use that
#define CONFIG_FILE "/etc/ipban/routes.toml"

// --- Dynamic array for storing routes ---
typedef struct {
    char **routes;
    int count;
    int capacity;
} RouteArray;

// Initialize route array
void init_route_array(RouteArray *array, int initial_capacity) {
    if (initial_capacity <= 0) initial_capacity = 10; // Ensure positive capacity
    array->routes = (char**)malloc(initial_capacity * sizeof(char*));
    if (!array->routes) {
        perror("Failed to allocate memory for route array");
        exit(EXIT_FAILURE);
    }
    array->count = 0;
    array->capacity = initial_capacity;
}

// Add route to array
void add_route(RouteArray *array, const char *route) {
    // Check for duplicates (optional, but good practice)
    for (int i = 0; i < array->count; i++) {
        if (strcmp(array->routes[i], route) == 0) {
            //printf("Debug: Duplicate route found, skipping: %s\n", route);
            return; // Don't add duplicates
        }
    }

    // Increase array size if necessary
    if (array->count >= array->capacity) {
        int new_capacity = array->capacity * 2;
        if (new_capacity < array->count + 1) new_capacity = array->count + 10; // Ensure growth
        char **new_routes = (char**)realloc(array->routes, new_capacity * sizeof(char*));
        if (!new_routes) {
            perror("Failed to reallocate memory for route array");
            fprintf(stderr, "Warning: Could not expand route array capacity.\n");
            // Consider exiting or handling memory exhaustion more gracefully
            return;
        }
        array->routes = new_routes;
        array->capacity = new_capacity;
    }

    // Copy route
    array->routes[array->count] = strdup(route);
    if (!array->routes[array->count]) {
        perror("Failed to duplicate route string");
        // Handle error appropriately
        return; // Or exit
    }
    array->count++;
}

// Free memory for RouteArray
void free_route_array(RouteArray *array) {
    if (array->routes) {
        for (int i = 0; i < array->count; i++) {
            free(array->routes[i]);
        }
        free(array->routes);
    }
    array->routes = NULL;
    array->count = 0;
    array->capacity = 0;
}

// --- Dynamic array for storing AS numbers ---
typedef struct {
    char **asns;
    int count;
    int capacity;
} AsnArray;

// Initialize ASN array
void init_asn_array(AsnArray *array, int initial_capacity) {
    if (initial_capacity <= 0) initial_capacity = 10; // Ensure positive capacity
    array->asns = (char**)malloc(initial_capacity * sizeof(char*));
    if (!array->asns) {
        perror("Failed to allocate memory for ASN array");
        exit(EXIT_FAILURE);
    }
    array->count = 0;
    array->capacity = initial_capacity;
}

// Add ASN to array
void add_asn(AsnArray *array, const char *asn) {
    // Check for duplicates (optional)
    for (int i = 0; i < array->count; i++) {
        if (strcmp(array->asns[i], asn) == 0) {
            return; // Don't add duplicates
        }
    }

    // Increase array size if necessary
    if (array->count >= array->capacity) {
        int new_capacity = array->capacity * 2;
        if (new_capacity < array->count + 1) new_capacity = array->count + 5; // Ensure growth
        char **new_asns = (char**)realloc(array->asns, new_capacity * sizeof(char*));
        if (!new_asns) {
            perror("Failed to reallocate memory for ASN array");
            fprintf(stderr, "Warning: Could not expand ASN array capacity.\n");
            return; // Or exit
        }
        array->asns = new_asns;
        array->capacity = new_capacity;
    }

    // Copy ASN
    array->asns[array->count] = strdup(asn);
    if (!array->asns[array->count]) {
        perror("Failed to duplicate ASN string");
        return; // Or exit
    }
    array->count++;
}

// Free memory for AsnArray
void free_asn_array(AsnArray *array) {
    if (array->asns) {
        for (int i = 0; i < array->count; i++) {
            free(array->asns[i]);
        }
        free(array->asns);
    }
    array->asns = NULL;
    array->count = 0;
    array->capacity = 0;
}


// --- Helper Functions ---

// Trim leading/trailing whitespace from a string in-place
char *trim_whitespace(char *str) {
    if (!str) return NULL;
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}


// Function to execute system command
void execute_command(const char *cmd) {
    printf("Executing: %s\n", cmd);
    int ret = system(cmd); // system() returns format related to wait() status

    if (ret == -1) {
        perror("system() failed"); // Error invoking shell or command
    } else if (WIFEXITED(ret)) {
        int exit_status = WEXITSTATUS(ret);
        if (exit_status != 0) {
            // Check if it's a 'del' command and the error might be "No such file or directory" (typical for non-existent route)
            // ESRCH (No such process) from `ip route del` also maps to exit code 2 in some cases.
            // Exit code 254 can also indicate object doesn't exist for `ip route del`.
            if (strstr(cmd, " del ") != NULL && (exit_status == 2 || exit_status == 254)) {
                fprintf(stderr, "Info: Command likely failed because route didn't exist: %s (exit code: %d)\n", cmd, exit_status);
            } else {
                fprintf(stderr, "Error executing command: %s (exit code: %d)\n", cmd, exit_status);
                // Consider if failure should be fatal depending on the command (e.g., adding routes)
                // if (strstr(cmd, " add ")) exit(EXIT_FAILURE);
            }
        }
        // Exit status 0 means success, do nothing.
    } else if (WIFSIGNALED(ret)) {
        fprintf(stderr, "Error: Command terminated by signal %d: %s\n", WTERMSIG(ret), cmd);
    } else {
        fprintf(stderr, "Error: Command terminated abnormally: %s (raw return: %d)\n", cmd, ret);
    }
}

// --- Configuration Reading ---

// Function to read configuration file (routes and ASNs)
int read_config(const char *filename, const char *route_section_v4, RouteArray *routes_v4,
                const char *route_section_v6, RouteArray *routes_v6,
                const char *asn_section, AsnArray *asns) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        char error_buf[512];
        snprintf(error_buf, sizeof(error_buf), "Failed to open configuration file '%s'", filename);
        perror(error_buf);
        return -1;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    char current_section[256] = "";
    int line_num = 0;

    while ((line_len = getline(&line, &line_cap, file)) > 0) {
        line_num++;

        // Strip comments early
        char *comment_char = strchr(line, '#');
        if (comment_char) *comment_char = '\0';
        comment_char = strchr(line, ';');
        if (comment_char) *comment_char = '\0';

        // Remove newline and trim whitespace
        line[strcspn(line, "\n")] = 0;
        char* trimmed_line = trim_whitespace(line);
        if (!trimmed_line || strlen(trimmed_line) == 0) {
            continue; // Skip empty or comment-only lines
        }
        line_len = strlen(trimmed_line);

        // Look for section start
        if (trimmed_line[0] == '[' && trimmed_line[line_len - 1] == ']') {
            strncpy(current_section, trimmed_line + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            char *closing_bracket = strchr(current_section, ']');
            if (closing_bracket) *closing_bracket = '\0';
            trim_whitespace(current_section);
            continue;
        }

        // Process key = value lines within sections
        char *key = strtok(trimmed_line, "=");
        char *value = strtok(NULL, "=");

        if (!key || !value) {
            continue; // Skip lines not in key=value format
        }

        key = trim_whitespace(key);
        value = trim_whitespace(value);

        if (!key || !value || strlen(key) == 0 || strlen(value) == 0) {
            continue; // Skip if key or value became empty after trim
        }


        RouteArray* target_routes = NULL;
        // Determine target based on section and key
        if (strcmp(current_section, route_section_v4) == 0 && strcmp(key, "routes") == 0) {
            target_routes = routes_v4;
        } else if (strcmp(current_section, route_section_v6) == 0 && strcmp(key, "routes") == 0) {
            target_routes = routes_v6;
        } else if (strcmp(current_section, asn_section) == 0 && strcmp(key, "as_numbers") == 0) {
            // Process ASNs list
            if (strlen(value) >= 2 && value[0] == '[' && value[strlen(value) - 1] == ']') {
                value[strlen(value) - 1] = '\0'; // Remove ']'
                value++; // Skip '['

                if (strlen(value) == 0) continue; // Handle empty list "[]"

                char *item_copy = strdup(value);
                if(!item_copy) { perror("strdup failed for ASN list"); continue; }

                char *token = strtok(item_copy, ",");
                while (token) {
                    char* asn_val = trim_whitespace(token);
                    if (asn_val && strlen(asn_val) > 0) {
                        if (strlen(asn_val) >= 2 && asn_val[0] == '"' && asn_val[strlen(asn_val) - 1] == '"') {
                            asn_val[strlen(asn_val) - 1] = '\0';
                            asn_val++;
                        }
                        asn_val = trim_whitespace(asn_val);
                        if(asn_val && strlen(asn_val) > 0) {
                            // Basic validation
                            char *endptr;
                            const char *check_asn = (strncmp(asn_val, "AS", 2) == 0 || strncmp(asn_val, "as", 2) == 0) ? asn_val + 2 : asn_val;
                            strtol(check_asn, &endptr, 10);

                            if (*endptr == '\0' && strlen(check_asn) > 0) { // Must be numeric after optional AS and non-empty
                                add_asn(asns, asn_val); // Add original format (with AS if present)
                            } else {
                                fprintf(stderr, "Warning: Invalid ASN format '%s' on line %d\n", asn_val, line_num);
                            }
                        }
                    }
                    token = strtok(NULL, ",");
                }
                free(item_copy);
            } else {
                fprintf(stderr, "Warning: Malformed AS number list value on line %d: '%s'\n", line_num, value);
            }
            continue; // Handled ASN line
        } // end ASN processing

        // Process routes list (common logic for v4 and v6)
        if (target_routes) {
            if (strlen(value) >= 2 && value[0] == '[' && value[strlen(value) - 1] == ']') {
                value[strlen(value) - 1] = '\0'; // Remove ']'
                value++; // Skip '['

                if (strlen(value) == 0) continue; // Handle empty list "[]"

                char *item_copy = strdup(value);
                if(!item_copy) { perror("strdup failed for route list"); continue; }

                char *token = strtok(item_copy, ",");
                while (token) {
                    char* route_val = trim_whitespace(token);
                    if (route_val && strlen(route_val) > 0) {
                        if (strlen(route_val) >= 2 && route_val[0] == '"' && route_val[strlen(route_val) - 1] == '"') {
                            route_val[strlen(route_val) - 1] = '\0';
                            route_val++;
                        }
                        route_val = trim_whitespace(route_val);
                        if(route_val && strlen(route_val) > 0) {
                            // Basic validation: check for '/'
                            if (strchr(route_val, '/')) {
                                add_route(target_routes, route_val);
                            } else {
                                fprintf(stderr, "Warning: Invalid route format (missing '/') '%s' on line %d\n", route_val, line_num);
                            }
                        }
                    }
                    token = strtok(NULL, ",");
                }
                free(item_copy);
            } else {
                fprintf(stderr, "Warning: Malformed route list value on line %d: '%s'\n", line_num, value);
            }
        } // end route processing

    } // end while getline

    free(line);
    fclose(file);

    return 0; // Indicate success
                }


                // --- ASN Prefix Fetching ---

                // Function to fetch prefixes for an ASN using bgpq4 and add them to route arrays - CORRECTED for plain output
                int fetch_and_add_prefixes(const char *asn, RouteArray *routes_v4, RouteArray *routes_v6) {
                    char cmd[512];
                    FILE *fp;
                    // Increased buffer size slightly just in case of weird long lines from bgpq4/popen
                    char prefix_buffer[INET6_ADDRSTRLEN + 20];
                    int added_count = 0;
                    int pclose_ret; // Store return value of pclose

                    // Ensure ASN format is suitable for bgpq4 (remove AS prefix if present)
                    const char *asn_num_str = asn;
                    if ((strncmp(asn, "AS", 2) == 0 || strncmp(asn, "as", 2) == 0) && strlen(asn) > 2) {
                        asn_num_str = asn + 2;
                    } else {
                        // Validate if it's just a number
                        char *endptr;
                        strtol(asn, &endptr, 10);
                        if (*endptr != '\0' || strlen(asn) == 0) { // Check if conversion consumed the string and it wasn't empty
                            fprintf(stderr, "Warning: Invalid ASN format '%s', skipping fetch.\n", asn);
                            return 0; // Skip this ASN
                        }
                        // It's just a number, use it directly
                        asn_num_str = asn;
                    }

                    printf("Fetching prefixes for AS%s...\n", asn_num_str);

                    // --- Fetch IPv4 prefixes ---
                    // Use -A for aggregation and -F for plain format. Note escaped format string.
                    snprintf(cmd, sizeof(cmd), "%s -4 -A -F '%%n/%%l\\n' AS%s", BGPQ_COMMAND, asn_num_str);

                    fp = popen(cmd, "r");
                    if (fp == NULL) {
                        fprintf(stderr, "Error: Failed to run command: %s\n", cmd);
                        perror("popen failed");
                        return -1; // Indicate failure to run command
                    }

                    while (fgets(prefix_buffer, sizeof(prefix_buffer), fp) != NULL) {
                        prefix_buffer[strcspn(prefix_buffer, "\n")] = 0; // Remove trailing newline
                        char* trimmed_prefix = trim_whitespace(prefix_buffer);
                        // Check if it's a valid prefix (contains '/' and isn't empty)
                        if (trimmed_prefix && strlen(trimmed_prefix) > 0 && strchr(trimmed_prefix, '/')) {
                            // Further validation could use inet_pton(AF_INET, ...) here if needed
                            printf("  Adding IPv4 prefix from AS%s: %s\n", asn_num_str, trimmed_prefix);
                            add_route(routes_v4, trimmed_prefix);
                            added_count++;
                        } else if (trimmed_prefix && strlen(trimmed_prefix) > 0) {
                            // Log unexpected non-prefix output if needed
                            // fprintf(stderr, "  Skipping non-prefix output (v4) for AS%s: %s\n", asn_num_str, trimmed_prefix);
                        }
                    }
                    pclose_ret = pclose(fp);
                    if (pclose_ret == -1) {
                        perror("pclose failed after IPv4 prefix fetch");
                    } else if (WIFEXITED(pclose_ret)) {
                        int exit_status = WEXITSTATUS(pclose_ret);
                        if (exit_status != 0) {
                            fprintf(stderr, "Warning: Command '%s' exited with status %d\n", cmd, exit_status);
                        }
                    } else if (WIFSIGNALED(pclose_ret)) {
                        fprintf(stderr, "Warning: Command '%s' terminated by signal %d\n", cmd, WTERMSIG(pclose_ret));
                    } else {
                        fprintf(stderr, "Warning: Command '%s' terminated abnormally (raw return: %d)\n", cmd, pclose_ret);
                    }

                    // --- Fetch IPv6 prefixes ---
                    snprintf(cmd, sizeof(cmd), "%s -6 -A -F '%%n/%%l\\n' AS%s", BGPQ_COMMAND, asn_num_str);

                    fp = popen(cmd, "r");
                    if (fp == NULL) {
                        fprintf(stderr, "Error: Failed to run command: %s\n", cmd);
                        perror("popen failed");
                        // Decide if this is fatal. Returning -1 indicates *some* part failed.
                        return -1;
                    }

                    while (fgets(prefix_buffer, sizeof(prefix_buffer), fp) != NULL) {
                        prefix_buffer[strcspn(prefix_buffer, "\n")] = 0; // Remove trailing newline
                        char* trimmed_prefix = trim_whitespace(prefix_buffer);
                        // Check if it's a valid prefix (contains '/' and isn't empty)
                        if (trimmed_prefix && strlen(trimmed_prefix) > 0 && strchr(trimmed_prefix, '/')) {
                            // Further validation could use inet_pton(AF_INET6, ...) here if needed
                            printf("  Adding IPv6 prefix from AS%s: %s\n", asn_num_str, trimmed_prefix);
                            add_route(routes_v6, trimmed_prefix);
                            added_count++;
                        } else if (trimmed_prefix && strlen(trimmed_prefix) > 0) {
                            // Log unexpected non-prefix output if needed
                            // fprintf(stderr, "  Skipping non-prefix output (v6) for AS%s: %s\n", asn_num_str, trimmed_prefix);
                        }
                    }
                    pclose_ret = pclose(fp);
                    if (pclose_ret == -1) {
                        perror("pclose failed after IPv6 prefix fetch");
                    } else if (WIFEXITED(pclose_ret)) {
                        int exit_status = WEXITSTATUS(pclose_ret);
                        if (exit_status != 0) {
                            fprintf(stderr, "Warning: Command '%s' exited with status %d\n", cmd, exit_status);
                        }
                    } else if (WIFSIGNALED(pclose_ret)) {
                        fprintf(stderr, "Warning: Command '%s' terminated by signal %d\n", cmd, WTERMSIG(pclose_ret));
                    } else {
                        fprintf(stderr, "Warning: Command '%s' terminated abnormally (raw return: %d)\n", cmd, pclose_ret);
                    }


                    if (added_count == 0) {
                        // This message might appear if the ASN is valid but has no public routes, or if bgpq4 failed (warning printed above)
                        printf("  No valid prefixes found or added for AS%s via %s.\n", asn_num_str, BGPQ_COMMAND);
                    }

                    // Return number of prefixes added (or -1 if popen failed)
                    return added_count;
                }


                // --- Main Function ---

                int main() {
                    RouteArray ipv4_routes, ipv6_routes;
                    AsnArray asns_to_block;

                    init_route_array(&ipv4_routes, 50);
                    init_route_array(&ipv6_routes, 50);
                    init_asn_array(&asns_to_block, 10);

                    // Read configuration
                    printf("Reading configuration from %s...\n", CONFIG_FILE);
                    if (read_config(CONFIG_FILE, "ipv4_routes", &ipv4_routes,
                        "ipv6_routes", &ipv6_routes,
                        "asn_block", &asns_to_block) == -1) {
                        fprintf(stderr, "Failed to read or parse configuration file. Exiting.\n");
                    free_route_array(&ipv4_routes);
                    free_route_array(&ipv6_routes);
                    free_asn_array(&asns_to_block);
                    return 1;
                        }

                        printf("Read %d direct IPv4 routes, %d direct IPv6 routes, and %d ASNs to block.\n",
                               ipv4_routes.count, ipv6_routes.count, asns_to_block.count);


                        // Fetch prefixes for specified ASNs
                        printf("\nFetching prefixes for ASNs specified in config...\n");
                        int total_fetched_prefixes = 0;
                        int asn_fetch_failed = 0; // Flag if any fetch command failed to run
                        for (int i = 0; i < asns_to_block.count; i++) {
                            int fetched_count = fetch_and_add_prefixes(asns_to_block.asns[i], &ipv4_routes, &ipv6_routes);
                            if (fetched_count > 0) {
                                total_fetched_prefixes += fetched_count;
                            } else if (fetched_count == -1) {
                                // Error message already printed by fetch_and_add_prefixes (popen failure)
                                fprintf(stderr, "Warning: Failed to execute prefix fetch for %s. Continuing...\n", asns_to_block.asns[i]);
                                asn_fetch_failed = 1; // Mark that at least one fetch failed
                            }
                            // If fetched_count is 0, message already printed inside the function.
                        }
                        printf("Finished fetching ASN prefixes. Added %d prefixes from ASN lookups.\n", total_fetched_prefixes);
                        printf("Total unique IPv4 routes to manage: %d\n", ipv4_routes.count);
                        printf("Total unique IPv6 routes to manage: %d\n", ipv6_routes.count);

                        if (asn_fetch_failed) {
                            fprintf(stderr, "Warning: One or more ASN prefix lookups failed to execute. Route list may be incomplete.\n");
                            // Optionally exit here if this is critical:
                            // free_route_array(&ipv4_routes); free_route_array(&ipv6_routes); free_asn_array(&asns_to_block); return 1;
                        }

                        // --- Apply Routes ---

                        // Delete existing blackhole routes (Best Effort)
                        printf("\n--- Deleting Blackhole Routes (Best Effort) ---\n");
                        if (ipv4_routes.count > 0) {
                            printf("Attempting to delete %d IPv4 routes...\n", ipv4_routes.count);
                            for (int i = 0; i < ipv4_routes.count; i++) {
                                size_t cmd_len = strlen(ipv4_routes.routes[i]) + 30;
                                char *cmd = (char*)malloc(cmd_len);
                                if (!cmd) { perror("malloc failed for delete command (IPv4)"); continue; }
                                snprintf(cmd, cmd_len, "ip -4 route del blackhole %s", ipv4_routes.routes[i]);
                                execute_command(cmd); // Handles its own error reporting
                                free(cmd);
                            }
                        } else {
                            printf("No IPv4 routes specified to delete.\n");
                        }

                        if (ipv6_routes.count > 0) {
                            printf("Attempting to delete %d IPv6 routes...\n", ipv6_routes.count);
                            for (int i = 0; i < ipv6_routes.count; i++) {
                                size_t cmd_len = strlen(ipv6_routes.routes[i]) + 30;
                                char *cmd = (char*)malloc(cmd_len);
                                if (!cmd) { perror("malloc failed for delete command (IPv6)"); continue; }
                                snprintf(cmd, cmd_len, "ip -6 route del blackhole %s", ipv6_routes.routes[i]);
                                execute_command(cmd); // Handles its own error reporting
                                free(cmd);
                            }
                        } else {
                            printf("No IPv6 routes specified to delete.\n");
                        }
                        printf("---------------------------------------------\n");


                        // Add new Blackhole routes
                        printf("\n--- Adding Blackhole Routes ---\n");
                        if (ipv4_routes.count > 0) {
                            printf("Adding %d IPv4 routes...\n", ipv4_routes.count);
                            for (int i = 0; i < ipv4_routes.count; i++) {
                                size_t cmd_len = strlen(ipv4_routes.routes[i]) + 30;
                                char *cmd = (char*)malloc(cmd_len);
                                if (!cmd) { perror("malloc failed for add command (IPv4)"); continue; }
                                snprintf(cmd, cmd_len, "ip -4 route add blackhole %s", ipv4_routes.routes[i]);
                                execute_command(cmd); // Handles its own error reporting
                                free(cmd);
                            }
                        } else {
                            printf("No IPv4 routes specified to add.\n");
                        }

                        if (ipv6_routes.count > 0) {
                            printf("Adding %d IPv6 routes...\n", ipv6_routes.count);
                            for (int i = 0; i < ipv6_routes.count; i++) {
                                size_t cmd_len = strlen(ipv6_routes.routes[i]) + 30;
                                char *cmd = (char*)malloc(cmd_len);
                                if (!cmd) { perror("malloc failed for add command (IPv6)"); continue; }
                                snprintf(cmd, cmd_len, "ip -6 route add blackhole %s", ipv6_routes.routes[i]);
                                execute_command(cmd); // Handles its own error reporting
                                free(cmd);
                            }
                        } else {
                            printf("No IPv6 routes specified to add.\n");
                        }
                        printf("-------------------------------\n");


                        // Print final routes (optional)
                        printf("\n--- Final Routes After Addition Attempt ---\n");
                        execute_command("ip -4 route show");
                        execute_command("ip -6 route show");
                        printf("-----------------------------------------\n");


                        // Free memory
                        printf("\nCleaning up resources...\n");
                        free_route_array(&ipv4_routes);
                        free_route_array(&ipv6_routes);
                        free_asn_array(&asns_to_block);

                        printf("Done.\n");
                        return 0;
                }
