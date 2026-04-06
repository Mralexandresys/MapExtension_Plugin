# Local test

Mock API locale pour `mapview`.

Endpoints exposes :

- `/health`
- `/cargo`
- `/rupture-cycle`

Lancement :

```bash
cd MapExtension_Plugin/mapview
npm run mock-api
```

Endpoint a utiliser dans l'UI :

```text
127.0.0.1:9000
```

Snapshots optionnels a deposer dans `local-test/data/` :

- `health.json`
- `cargo.json`
- `rupture-cycle.json` ou `rupture_cycle.json`

Si ces fichiers sont absents, le serveur repond avec des payloads mock minimaux.
