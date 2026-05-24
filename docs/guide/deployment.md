# Deployment

Aevox is designed to run as a private HTTP/1.1 application server behind a reverse proxy. Put Caddy
or Nginx on the public edge for TLS, HTTP/2, HTTP/3, compression, caching, and internet-facing
protocol negotiation.

## Production Shape

The supported production shape is:

```text
Client
  -> HTTPS / HTTP/2 / HTTP/3
Caddy or Nginx
  -> HTTP/1.1 on 127.0.0.1:8080
Aevox app
```

Bind the Aevox listener to a private interface such as `127.0.0.1:8080` and expose only the reverse
proxy to the public network. Aevox core does not terminate TLS and does not serve native HTTP/2 or
HTTP/3.

## Caddy

Caddy is the shortest path for public HTTPS because it manages certificates automatically. This
Caddyfile serves `example.com` and proxies requests to an Aevox app on `127.0.0.1:8080`:

```caddyfile
example.com {
    encode zstd gzip

    reverse_proxy 127.0.0.1:8080 {
        header_up Host {host}
        header_up X-Forwarded-For {remote_host}
        header_up X-Forwarded-Proto {scheme}
    }
}
```

Caddy negotiates HTTP/2 and HTTP/3 with clients when the deployment environment supports them. The
upstream connection to Aevox remains plain HTTP/1.1.

## Nginx

Nginx needs explicit certificate paths. This server block terminates TLS on `443` and proxies to an
Aevox app on `127.0.0.1:8080`:

```nginx
server {
    listen 443 ssl http2;
    server_name example.com;

    ssl_certificate /etc/letsencrypt/live/example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

    location / {
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_pass http://127.0.0.1:8080;
    }
}
```

Certificate issuance and renewal belong to your deployment tooling. Aevox only receives the proxied
HTTP/1.1 request.

## WebSocket Proxying

WebSocket routes still begin as HTTP/1.1 upgrade requests at the Aevox listener. Caddy forwards
WebSocket upgrades through `reverse_proxy` automatically:

```caddyfile
example.com {
    reverse_proxy 127.0.0.1:8080
}
```

For Nginx, preserve the upgrade headers and use HTTP/1.1 to the upstream:

```nginx
map $http_upgrade $connection_upgrade {
    default upgrade;
    '' close;
}

server {
    listen 443 ssl http2;
    server_name example.com;

    ssl_certificate /etc/letsencrypt/live/example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

    location / {
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $connection_upgrade;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 1h;
        proxy_pass http://127.0.0.1:8080;
    }
}
```

Increase proxy read timeouts for long-lived connections such as chat, game, telemetry, and dashboard
streams.

## gRPC Boundary

Normal Aevox HTTP routes run through the core HTTP/1.1 listener, such as `127.0.0.1:8080`. The
official gRPC plugin is different: it listens on its own h2c port, for example `127.0.0.1:50051`,
using its plugin-local HTTP/2 stack.

Do not proxy gRPC traffic to the core HTTP/1.1 port. Public TLS for gRPC should terminate at the
reverse proxy and forward to the gRPC plugin's h2c listener:

```text
gRPC client
  -> TLS / HTTP/2
Caddy or Nginx
  -> h2c on 127.0.0.1:50051
Aevox gRPC plugin
```

## Forwarded Headers

Forward the original request context to application code:

| Header | Purpose |
|---|---|
| `Host` | Preserves the public host requested by the client |
| `X-Forwarded-For` | Carries the client address chain through the proxy |
| `X-Forwarded-Proto` | Records whether the original client request used `http` or `https` |
| `traceparent` | Propagates W3C trace context across services when present |

Aevox exposes request headers to application code. It does not yet provide a dedicated
trusted-proxy abstraction, so treat forwarded headers as trusted only when they are set by a proxy
you control and the Aevox backend port is not publicly reachable.

## Static Assets and Caching

Use the reverse proxy or a CDN for high-volume public static assets. The
`aevox::middleware::static_files()` middleware is useful for application-owned files and local
development, but the proxy layer is the right place for public cache policy, compression, and edge
delivery.

For Nginx, static cache policy usually belongs in a dedicated `location` block before the proxy
fallback:

```nginx
location /assets/ {
    root /srv/example-app/public;
    expires 1h;
    add_header Cache-Control "public";
}
```

## Validation

Validate the proxy configuration before reload:

```bash
caddy validate --config Caddyfile
```

Expected result: Caddy reports that the configuration is valid.

```bash
nginx -t -c /absolute/path/to/nginx.conf
```

Expected result: Nginx reports successful syntax and configuration checks.

Validate the documentation site after editing:

```bash
mkdocs build --strict
```

Expected result: MkDocs exits with no warnings.

## See Also

- [Configuration](configuration.md) - binding Aevox to a private host and port
- [WebSocket](websocket.md) - registering WebSocket routes before proxying upgrades
- [gRPC](grpc.md) - enabling the optional gRPC plugin on a separate h2c port
- [Static Files](static-files.md) - serving application-owned assets
