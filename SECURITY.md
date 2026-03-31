# Security Policy

## Scope

iWetMyPlants is a local-network plant monitoring system. It is **not designed to be exposed to the public internet** — devices are intended to operate within your home network only.

That said, if you find a security issue, please report it responsibly.

## Reporting a Vulnerability

**Please do not open a public GitHub issue for security vulnerabilities.**

Instead, reach out privately via the contact form on the [installer page](https://spiritwrestler.ca/plants) (use the Feedback button and select "Bug Report" — note in the description that it's a security matter and include an email address so I can follow up privately).

I aim to acknowledge reports within 72 hours and will work toward a fix as quickly as the issue warrants.

## What Counts as a Security Issue?

- Unauthenticated access to device APIs that could allow unauthorized control (e.g., triggering relays)
- Credentials or secrets exposed in firmware, source code, or documentation
- Vulnerabilities in the web installer that could affect visitors
- Anything that could allow a device to be compromised and used to attack other systems on the local network

## What Doesn't Count

- The fact that the web UI has no authentication by default — this is intentional and documented. The expectation is that your home network is trusted. If you need auth, it's on the roadmap.
- Issues that require physical access to the device (serial console, flash chip access, etc.) — physical access implies trust in this context
- Issues in third-party dependencies (ESP-IDF, Arduino framework, ESP Web Tools) — please report those upstream
