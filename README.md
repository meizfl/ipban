# IPBan
IPBan - program for blocking IP addresses and IP subnets based on iproute2 in accordance with the config /etc/ipban/routes.toml

Using:
========
example config:
```
[ipv4_routes]
routes = ["192.0.2.0/24"]

[ipv6_routes]
routes = ["2001:db8::/32"]

[asn_block]
as_numbers = ["AS1234"]
```
the config is located at /etc/ipban/routes.toml
