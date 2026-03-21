
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description :  EVPN Route Information Base
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_rib.c  
 * Purpose     : 
 *                This includes :
 *                  - Manages storage and lookup of EVPN routes
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include "../include/evpn.h"

/**
 * Add route to RIB
 */
int evpn_rib_add(evpn_ctx_t *ctx, evpn_rib_entry_t *route) {
    if (!ctx || !route) {
        return -1;
    }
    
    /* Allocate new entry */
    evpn_rib_entry_t *entry = (evpn_rib_entry_t *)malloc(sizeof(evpn_rib_entry_t));
    if (!entry) {
        perror("malloc rib entry");
        return -1;
    }
    
    /* Copy route data */
    memcpy(entry, route, sizeof(evpn_rib_entry_t));
    entry->timestamp = time(NULL);
    entry->next = NULL;
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    /* Add to head of list */
    entry->next = ctx->rib;
    ctx->rib = entry;
    ctx->rib_count++;
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    /* Log based on route type */
    if (entry->type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT) {
        printf("RIB: Added MAC/IP route: %02x:%02x:%02x:%02x:%02x:%02x",
               entry->route.mac_ip.mac[0], entry->route.mac_ip.mac[1],
               entry->route.mac_ip.mac[2], entry->route.mac_ip.mac[3],
               entry->route.mac_ip.mac[4], entry->route.mac_ip.mac[5]);
        
        if (entry->route.mac_ip.ip_len > 0) {
            struct in_addr addr;
            addr.s_addr = entry->route.mac_ip.ip;
            printf(" / %s", inet_ntoa(addr));
        }
        
        printf(" (VNI %u)\n", entry->route.mac_ip.label1);
    } else if (entry->type == EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST) {
        struct in_addr addr;
        addr.s_addr = entry->route.inclusive_mcast.originating_router_ip;
        printf("RIB: Added Inclusive Multicast route from %s (VNI %u)\n",
               inet_ntoa(addr), entry->route.inclusive_mcast.vni);
    } else {
        printf("RIB: Added route type %d\n", entry->type);
    }
    
    return 0;
}

/**
 * Remove route from RIB
 */
int evpn_rib_remove(evpn_ctx_t *ctx, evpn_route_type_t type, 
                    const uint8_t *mac, uint32_t vni) {
    if (!ctx) {
        return -1;
    }
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *prev = NULL;
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry) {
        bool match = false;
        
        if (entry->type == type) {
            if (type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT && mac) {
                if (memcmp(entry->route.mac_ip.mac, mac, 6) == 0 &&
                    entry->route.mac_ip.label1 == vni) {
                    match = true;
                }
            } else if (type == EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST) {
                if (entry->route.inclusive_mcast.vni == vni) {
                    match = true;
                }
            }
        }
        
        if (match) {
            /* Remove from list */
            if (prev) {
                prev->next = entry->next;
            } else {
                ctx->rib = entry->next;
            }
            
            ctx->rib_count--;
            pthread_mutex_unlock(&ctx->rib_lock);
            
            printf("RIB: Removed route type %d (VNI %u)\n", type, vni);
            
            free(entry);
            return 0;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    return -1;  /* Not found */
}

/**
 * Lookup route in RIB (MAC/IP advertisement)
 */
evpn_rib_entry_t *evpn_rib_lookup_mac(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni) {
    if (!ctx || !mac) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry) {
        if (entry->type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT &&
            memcmp(entry->route.mac_ip.mac, mac, 6) == 0 &&
            entry->route.mac_ip.label1 == vni) {
            
            pthread_mutex_unlock(&ctx->rib_lock);
            return entry;
        }
        
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    return NULL;
}

/**
 * Lookup inclusive multicast route for VNI
 */
evpn_rib_entry_t *evpn_rib_lookup_inclusive_mcast(evpn_ctx_t *ctx, uint32_t vni) {
    if (!ctx) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry) {
        if (entry->type == EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST &&
            entry->route.inclusive_mcast.vni == vni) {
            
            pthread_mutex_unlock(&ctx->rib_lock);
            return entry;
        }
        
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    return NULL;
}

/**
 * Get all routes of a specific type
 */
int evpn_rib_get_routes_by_type(evpn_ctx_t *ctx, evpn_route_type_t type,
                                 evpn_rib_entry_t **routes, int max_routes) {
    if (!ctx || !routes) {
        return -1;
    }
    
    int count = 0;
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry && count < max_routes) {
        if (entry->type == type) {
            routes[count] = entry;
            count++;
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    return count;
}

/**
 * Get all routes for a specific VNI
 */
int evpn_rib_get_routes_by_vni(evpn_ctx_t *ctx, uint32_t vni,
                                evpn_rib_entry_t **routes, int max_routes) {
    if (!ctx || !routes) {
        return -1;
    }
    
    int count = 0;
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry && count < max_routes) {
        bool match = false;
        
        if (entry->type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT) {
            if (entry->route.mac_ip.label1 == vni) {
                match = true;
            }
        } else if (entry->type == EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST) {
            if (entry->route.inclusive_mcast.vni == vni) {
                match = true;
            }
        }
        
        if (match) {
            routes[count] = entry;
            count++;
        }
        
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    return count;
}

/**
 * Clear all routes from RIB
 */
int evpn_rib_clear(evpn_ctx_t *ctx) {
    if (!ctx) {
        return -1;
    }
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    int count = 0;
    
    while (entry) {
        evpn_rib_entry_t *next = entry->next;
        free(entry);
        entry = next;
        count++;
    }
    
    ctx->rib = NULL;
    ctx->rib_count = 0;
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    printf("RIB: Cleared %d routes\n", count);
    
    return count;
}

/**
 * Age out old routes
 */
int evpn_rib_age_routes(evpn_ctx_t *ctx, time_t max_age) {
    if (!ctx) {
        return -1;
    }
    
    time_t now = time(NULL);
    int aged_count = 0;
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *prev = NULL;
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry) {
        if (now - entry->timestamp > max_age) {
            /* Age out this entry */
            evpn_rib_entry_t *next = entry->next;
            
            if (prev) {
                prev->next = next;
            } else {
                ctx->rib = next;
            }
            
            ctx->rib_count--;
            aged_count++;
            
            free(entry);
            entry = next;
        } else {
            prev = entry;
            entry = entry->next;
        }
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    if (aged_count > 0) {
        printf("RIB: Aged out %d routes (older than %ld seconds)\n", 
               aged_count, max_age);
    }
    
    return aged_count;
}

/**
 * Get RIB statistics
 */
void evpn_rib_get_stats(evpn_ctx_t *ctx, int *total, int *type2, int *type3) {
    if (!ctx) {
        return;
    }
    
    int count_total = 0;
    int count_type2 = 0;
    int count_type3 = 0;
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    
    while (entry) {
        count_total++;
        
        if (entry->type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT) {
            count_type2++;
        } else if (entry->type == EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST) {
            count_type3++;
        }
        
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    if (total) *total = count_total;
    if (type2) *type2 = count_type2;
    if (type3) *type3 = count_type3;
}
