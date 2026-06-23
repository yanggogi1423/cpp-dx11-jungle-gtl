def mirror_obj_x(input_path, output_path):
    with open(input_path, 'r') as f:
        lines = f.readlines()

    out_lines = []

    for line in lines:
        if line.startswith('v '):  # vertex
            parts = line.strip().split()
            x = -float(parts[1])
            y = float(parts[2])
            z = float(parts[3])
            out_lines.append(f"v {x} {y} {z}\n")

        elif line.startswith('vn '):  # normal
            parts = line.strip().split()
            x = -float(parts[1])
            y = float(parts[2])
            z = float(parts[3])
            out_lines.append(f"vn {x} {y} {z}\n")

        elif line.startswith('f '):  # face (핵심: winding 뒤집기)
            parts = line.strip().split()[1:]

            # 각 vertex (v/vt/vn 형태 유지)
            reversed_parts = list(reversed(parts))

            out_lines.append("f " + " ".join(reversed_parts) + "\n")

        else:
            out_lines.append(line)

    with open(output_path, 'w') as f:
        f.writelines(out_lines)


# 사용 예시
mirror_obj_x("cyber_arm_left_small.obj", "cyber_arm_right_small.obj")