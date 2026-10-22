# Data Directory

Place circuit-specific JSON files here. The backend will load them automatically.

## Format: `<circuit_name>_tyres.json`

```json
{
  "circuit": "monza",
  "year": "2023",
  "pit_loss_seconds": 21.5,
  "base_lap_time": 81.8,
  "compounds": [
    {
      "compound": "SOFT",
      "base_lap_time_delta": -0.5,
      "deg_linear": 0.09,
      "deg_quadratic": 0.003,
      "min_laps": 5,
      "max_laps": 28
    },
    {
      "compound": "MEDIUM",
      "base_lap_time_delta": 0.0,
      "deg_linear": 0.055,
      "deg_quadratic": 0.0018,
      "min_laps": 10,
      "max_laps": 38
    },
    {
      "compound": "HARD",
      "base_lap_time_delta": 0.45,
      "deg_linear": 0.03,
      "deg_quadratic": 0.001,
      "min_laps": 15,
      "max_laps": 55
    }
  ]
}
```

Data can be sourced from the [OpenF1 API](https://openf1.org) or [FastF1](https://theoehrly.github.io/Fast-F1/).
