import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# data reading
df = pd.read_csv('fps_log.csv')

# Convert difficulty levels to text
difficulty_map = {0: 'Easy', 1: 'Normal', 2: 'Hard'}
df['Difficulty'] = df['Difficulty'].map(difficulty_map)

# style
sns.set(style="darkgrid")
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

# FPS graph
for difficulty, group in df.groupby('Difficulty'):
    ax1.plot(group['Timestamp'], group['FPS'], label=difficulty, linewidth=2)
ax1.set_ylabel('FPS', fontsize=12)
ax1.set_title('Performance by difficulty level', fontsize=14)
ax1.legend()

# Frame time graph
for difficulty, group in df.groupby('Difficulty'):
    ax2.plot(group['Timestamp'], group['FrameTime'], label=difficulty, linewidth=2)
ax2.set_xlabel('Time (seconds)', fontsize=12)
ax2.set_ylabel('Frame time (ms)', fontsize=12)
ax2.legend()

plt.tight_layout()
plt.savefig('performance_plot.png', dpi=300)
plt.show()