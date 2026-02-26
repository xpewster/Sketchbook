# config.py - Config file parsing utilities

def parse_config(filepath):
    """Parse key=value config file into dictionary."""
    config = {}
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    key, value = line.split('=', 1)
                    key = key.strip()
                    value = value.strip()
                    # Try to parse as number
                    try:
                        if '.' in value:
                            config[key] = float(value)
                        else:
                            config[key] = int(value)
                    except ValueError:
                        config[key] = value
    except OSError as e:
        print(f"Config error: {e}")
    return config


def get_config(config, key, default=None):
    """Get config value with default."""
    return config.get(key, default)


def get_config_bool(config, key, default=False):
    """Get config value as boolean."""
    val = config.get(key, default)
    if isinstance(val, bool):
        return val
    if isinstance(val, int):
        return val != 0
    if isinstance(val, str):
        return val.lower() in ('true', '1', 'yes')
    return default