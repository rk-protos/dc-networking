# VXLAN Multicast & IGMP Support
## RFC 7348 Section 4.2 Complete Implementation

---

## Overview 

> **"To effect this, we need to have a mapping between the VXLAN VNI and the IP multicast group that it will use. This mapping is done at the management layer and provided to the individual VTEPs through a management channel. Using this mapping, the VTEP can provide IGMP membership reports to the upstream switch/router to join/leave the VXLAN-related IP multicast groups as needed."**

> **"The VTEP will use (*,G) joins. This is needed as the set of VXLAN tunnel sources is unknown and may change often, as the VMs come up / go down across different hosts."**

### Included Features:

| Requirement | 
|-------------| 
| VNI to multicast group mapping |
| IGMP membership reports (join) |
| IGMP leave messages | 
| (*,G) joins (any source) |
| BUM traffic via multicast |
| Broadcast frames via multicast |
| Unknown unicast via multicast |
| Multicast frames via multicast |

---


## BUM Traffic Types

**BUM = Broadcast, Unknown unicast, Multicast**

### 1. **Broadcast Traffic**
- Destination MAC: `FF:FF:FF:FF:FF:FF`
- Examples: ARP requests, DHCP discovery
- Sent to multicast group for VNI

### 2. **Unknown Unicast**
- Destination MAC not in MAC learning table
- Examples: First packet to new host
- Sent to multicast group until MAC is learned

### 3. **Multicast Traffic**
- Destination MAC: `01:XX:XX:XX:XX:XX` (multicast bit set)
- Examples: IPv4 multicast (01:00:5E:XX:XX:XX), IPv6 ND
- Sent to multicast group for VNI

---

## Work flow

```
┌─────────────────────────────────────────────────────────────┐
│                         VXLAN VTEP                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐       ┌──────────────┐                     │
│  │ Inner Frame │──────>│ BUM Traffic? │                     │
│  └─────────────┘       └──────┬───────┘                     │
│                               │                             │
│                         ┌─────┴─────┐                       │
│                         │    Yes    │    No                 │
│                         │           │                       │
│                         ▼           ▼                       │
│              ┌──────────────┐  ┌──────────┐                 │
│              │  Multicast   │  │ Unicast  │                 │
│              │   Send to    │  │  Direct  │                 │
│              │ Mcast Group  │  │   Send   │                 │
│              └──────────────┘  └──────────┘                 │
│                     │                                       │
│                     ▼                                       │
│           ┌──────────────────┐                              │
│           │  IGMP Join (VNI) │                              │
│           │  239.X.Y.Z       │                              │
│           └──────────────────┘                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                         │
                         ▼
            ┌────────────────────────┐
            │  IP Multicast Network  │
            │  (224.0.0.0-239.255...)│
            └────────────────────────┘
                         │
                         ▼
            ┌────────────────────────┐
            │   All VTEPs in VNI     │
            │   (joined same group)  │
            └────────────────────────┘
```

---

## Scenario Details


### Join Multicast Group

// Option 1: Auto-join (uses default VNI-to-group mapping)
Example: 
vni = 100
Group = 239.0.0.100

// Option 2: Manual group specification
Example: 
vni = 200; Group: "239.1.1.200"

### Leave Multicast Group

```c
// Sends IGMP Leave message
Example: vni = 100
```

### Send BUM Traffic
### Receive Multicast Traffic



## VNI to Multicast Group Mapping

### Default Mapping Algorithm

The implementation uses the **239.0.0.0/8** range for VXLAN multicast:

```
VNI → 239.VNI[23:16].VNI[15:8].VNI[7:0]
```

**Examples:**
- VNI 100 → 239.0.0.100
- VNI 1000 → 239.0.3.232
- VNI 0x123456 (1193046) → 239.18.52.86

**Benefits:**
- Automatic mapping (no configuration needed)
- 16M unique groups (entire VNI space)
- Deterministic and reversible

### Custom Mapping

We can also specify custom multicast groups:

```c
// Marketing department: VNI 100 → 239.10.0.1

// Engineering: VNI 200 → 239.20.0.1
```


## Network Infrastructure Requirements

### 1. **IP Multicast Support**

The network infrastructure must support IP multicast:

**Switches:**
- IGMP snooping (RFC 4541)
- Multicast group membership tracking

 **Routers:**
- PIM-SM (Protocol Independent Multicast - Sparse Mode)
- IGMP v2 or v3 support

### 2. **Recommended Protocols**

**For efficiency:**
- PIM-SM for multicast routing
- IGMP snooping for L2 optimization
- BIDIR-PIM (optional, more efficient for VXLAN)

### 3. **Firewall Configuration**

```bash
# Allow IGMP (protocol 2)
iptables -A INPUT -p igmp -j ACCEPT

# Allow multicast traffic (239.0.0.0/8)
iptables -A INPUT -d 239.0.0.0/8 -j ACCEPT
iptables -A OUTPUT -d 239.0.0.0/8 -j ACCEPT
```

---

## Deployment Scenarios

### Scenario 1: Data Center VXLAN

```c
// Each VTEP joins multicast group for its VNIs
vxlan_mcast_auto_join(&mcast_ctx, 100);  // Production
vxlan_mcast_auto_join(&mcast_ctx, 200);  // Development
vxlan_mcast_auto_join(&mcast_ctx, 300);  // Testing

// BUM traffic automatically distributed to all VTEPs in each VNI
```


### Scenario 2: Multi-Site Deployment

```c
// Site A VTEPs
vxlan_mcast_join(&mcast_ctx, 100, inet_addr("239.1.0.100"));

// Site B VTEPs  
vxlan_mcast_join(&mcast_ctx, 100, inet_addr("239.2.0.100"));

// Different multicast groups per site
// Controlled via multicast routing
```



## Notes (including security considerations):

- Network infrastructure must support **IP multicast**
- For deployment, ensure **IGMP snooping** is enabled on switches (security)
- Consider **PIM-SM** for efficient multicast routing across routers


