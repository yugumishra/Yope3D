from fontTools.varLib.instancer import instantiateVariableFont
from fontTools import ttLib

# Load the variable font
font_path = "nunito_sans.ttf"
output_path = "nunito_sans_bold.ttf"

# Instantiate at bold weight (wght=700 is standard bold)
vf = ttLib.TTFont(font_path)
instantiateVariableFont(vf, {"wght": 700})
vf.save(output_path)

print(f"Bold variant extracted to {output_path}")
