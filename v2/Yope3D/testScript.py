import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('yope_profile_1779987305.csv')
phys = df[df.thread == 'physics']
phys.query('scene == "Spring [Sphere] - Top Row Fixed"').groupby('stage')['duration_us'].plot(title='Physics stages', legend=True)
plt.xlabel('step'); plt.ylabel('µs'); plt.show(); plt.legend(loc='upper right');
