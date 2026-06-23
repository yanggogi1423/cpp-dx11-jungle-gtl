def parse_macro_args(arg_text: str):
    specifiers = []
    metadata = {}

    for token in [x.strip() for x in arg_text.split(",") if x.strip()]:
        if "=" in token:
            key, value = token.split("=", 1)
            metadata[key.strip()] = value.strip().strip('"')
        else:
            specifiers.append(token)

    return specifiers, metadata