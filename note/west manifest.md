## Writing a West manifest and organizing projects

The West manifest is a YAML file named `west.yml`

```
manifest:
	remotes:
		- name: zephyrproject
		url-base: https://github.com/zephyrproject-rtos
		
	projects:
		- name: zephyr
		remote: zephyrproject
		repo-path: zephyr
		revision: v4.3.0
```

它主要回答四个问题：

1. 工作区需要哪些仓库？
2. 从哪里获取这些仓库？
3. 使用哪个 Git 分支、标签或提交？
4. 每个仓库放在本地什么路径？