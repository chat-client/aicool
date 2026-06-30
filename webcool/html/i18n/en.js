(function () {
  var textMap = {
    // Shared window-control labels; keep keys aligned with zh.js.
    '清空回收站': 'Empty Trash',
    '确认清空回收站中的所有文件和目录？此操作不可恢复。': 'Empty all files and folders in Trash? This cannot be undone.',
    '清空': 'Empty',
    '回收站已经是空的': 'Trash is already empty',
    '回收站已清空，共删除 ': 'Trash emptied. Deleted ',
    ' 个项目': ' items',
    '文件管理控制台': 'File Management Console',
    '文件控制台': 'File Console',
    '左帧用于功能切换，右帧展示对应内容与操作结果。': 'Use the left pane to switch features; the right pane shows content and actions.',
    '功能菜单': 'Main menu',
    '上传文件': 'Upload Files',
    '虚拟磁盘': 'Virtual Disk',
    '本地磁盘': 'Local Disk',
    '系统设置': 'System Settings',
    '退出': 'Sign Out',
    '退出登录': 'Sign Out',
    '登录': 'Sign In',
    '注册管理员': 'Create Administrator',
    '首次使用，请创建管理员用户名和密码。': 'First use: create the administrator username and password.',
    '请输入用户名和密码。': 'Enter username and password.',
    '认证失败': 'Authentication failed',
    '认证状态检查失败': 'Authentication status check failed',
    '创建管理员': 'Create Administrator',
    '管理员': 'Administrator',
    '用户名': 'Username',
    '密码': 'Password',
    '用户管理': 'User Management',
    '管理普通用户账号和密码。': 'Manage regular user accounts and passwords.',
    '普通用户': 'Regular User',
    '管理员账号不会在此处修改或删除。': 'Administrator accounts cannot be modified or deleted here.',
    '添加用户': 'Add User',
    '账号设置': 'Account Settings',
    '账号设置功能命令': 'Account settings commands',
    '界面语言': 'Interface Language',
    '修改当前登录用户的密码和界面语言。': 'Change the password and interface language for the current signed-in user.',
    '修改当前登录用户的密码、界面语言与字体大小。': 'Change the password, interface language, and font size for the current signed-in user.',
    '修改当前登录用户的密码。': 'Change the password for the current signed-in user.',
    '当前密码': 'Current Password',
    '新密码': 'New Password',
    '确认新密码': 'Confirm New Password',
    '修改密码': 'Change Password',
    '暂无用户': 'No users yet',
    '修改': 'Edit',
    '用户已添加：': 'User added: ',
    '添加用户失败：': 'Failed to add user: ',
    '请输入新的用户名': 'Enter the new username',
    '用户名不能为空': 'Username cannot be empty',
    '请输入新密码；留空则不修改密码': 'Enter a new password; leave blank to keep it unchanged',
    '用户已更新：': 'User updated: ',
    '修改用户失败：': 'Failed to update user: ',
    '确认删除用户：': 'Delete user: ',
    '用户已删除：': 'User deleted: ',
    '删除用户失败：': 'Failed to delete user: ',
    '请输入当前密码和新密码。': 'Enter the current password and the new password.',
    '两次输入的新密码不一致。': 'The two new passwords do not match.',
    '密码已修改。': 'Password changed.',
    '修改密码失败：': 'Failed to change password: ',
    '系统设置功能命令': 'System settings commands',
    '文件标签树': 'File tag tree',
    '标签树': 'Tag Tree',
    '支持最多五十个文件同时上传，并展示实时上传进度。': 'Upload up to fifty files at once and show live progress.',
    '选择文件': 'Choose Files',
    '拖拽文件到此处上传': 'Drag files here to upload',
    '可一次选择或拖入最多 50 个文件': 'Choose or drop up to 50 files at once',
    '尚未选择文件': 'No files selected',
    '已选择 ': 'Selected ',
    '一次最多上传 50 个文件': 'Upload at most 50 files at once',
    '请先选择要上传的文件': 'Choose files to upload first',
    '选择上传目标目录': 'Choose Upload Target',
    '请选择这些文件要上传到虚拟磁盘中的哪个目录。': 'Choose which virtual disk folder these files should be uploaded to.',
    '从电脑拖入多个文件到此处，或拖到左侧目录树文件夹': 'Drag files from your computer here, or drop them onto a folder in the left tree',
    '上传到当前选中目录': 'Upload to the currently selected folder',
    '拖拽本地文件到此处，上传到虚拟磁盘': 'Drag local disk files here to upload to the virtual disk',
    '请先在「虚拟磁盘」中选择目标文件夹': 'Select a target folder in Virtual Disk first',
    '上传到：': 'Upload to: ',
    '当前目录：': 'Current folder: ',
    '请先在左侧目录树中选择目标文件夹': 'Select a target folder in the left folder tree first',
    '不能上传到此目录': 'Cannot upload to this folder',
    '开始上传': 'Start Upload',
    '稍后上传': 'Upload Later',
    '待上传 ': 'Pending upload: ',
    ' 项': ' items',
    '目标：': 'Target: ',
    '已添加 ': 'Added ',
    ' 个待上传项到队列。是否立即开始上传？': ' item(s) to the upload queue. Start uploading now?',
    '待上传队列已满或文件已存在，最多 ': 'Upload queue is full or files already exist. Maximum ',
    ' 项重复或超出上限': ' duplicate or skipped items',
    '，已忽略 ': ', ignored ',
    '添加待上传文件失败：': 'Failed to add files to upload queue: ',
    '确认删除': 'Confirm Delete',
    '移除标签引用': 'Remove Tag Reference',
    '此操作只解除标签引用，不会删除文件。': 'This only removes tag references and does not delete files.',
    '文件': 'file',
    '此操作不可恢复，请确认是否继续。': 'This action cannot be undone. Please confirm to continue.',
    '将先移入回收站，之后仍可在回收站中恢复或彻底删除。': 'Items will be moved to Trash first. You can restore or permanently delete them later from Trash.',
    '文件将移入回收站，不会立即永久删除。': 'Files will be moved to Trash and will not be permanently deleted immediately.',
    '彻底删除目录': 'Permanently Delete Folder',
    '此操作不可恢复，目录及其全部内容将被永久删除。': 'This action cannot be undone. The folder and all its contents will be permanently deleted.',
    '目录及其全部内容将移入回收站。': 'The folder and all its contents will be moved to Trash.',
    '移入回收站后仍可在回收站中恢复或彻底删除。': 'After moving to Trash, you can still restore or permanently delete items from Trash.',
    '文件将移至系统回收站。': 'The file will be moved to the system Trash.',
    '目录将移至系统回收站。': 'The folder will be moved to the system Trash.',
    '选中的本地文件将移至系统回收站。': 'Selected local files will be moved to the system Trash.',
    '选中的本地目录将移至系统回收站。': 'Selected local folders will be moved to the system Trash.',
    ' 个文件': ' files',
    ' 个目录': ' folders',
    '恢复文件': 'Restore Files',
    '将恢复到原路径；若目标位置已有同名文件，会自动改名。': 'Items will be restored to their original paths. If a name conflict exists, they will be renamed automatically.',
    '选中的文件将先移入回收站。': 'Selected files will be moved to Trash first.',
    '选中的文件夹会连同其全部内容一起删除。此操作不可恢复。': 'Selected folders will be deleted together with all their contents. This action cannot be undone.',
    ' 个': ' ',
    '准备上传...': 'Ready to upload...',
    '像资源管理器一样浏览多级文件夹，支持建删目录、预览、下载、删除和拖拽移动文件。': 'Browse folders like a file manager, with folder management, preview, download, delete, and drag-to-move.',
    '当前视图：全部文件': 'Current view: All files',
    '文件夹': 'Folders',
    '新建': 'Create',
    '删除': 'Delete',
    '恢复': 'Restore',
    '加载中...': 'Loading...',
    '当前目录文件': 'Current Folder Files',
    '当前标签文件': 'Current Tag Files',
    '列表': 'List',
    '预览': 'Preview',
    '源码': 'Source',
    'Markdown预览：': 'Markdown Preview: ',
    'HTML预览：': 'HTML Preview: ',
    'HTML预览': 'HTML Preview',
    '排序': 'Sort',
    '按名称': 'By name',
    '按大小': 'By size',
    '按上传时间': 'By upload time',
    '升序': 'Ascending',
    '降序': 'Descending',
    '文件名': 'File Name',
    '文件大小': 'File Size',
    '文件类型': 'File Type',
    '创建时间': 'Created',
    '文件摘要': 'File Summary',
    '摘要': 'Summary',
    '下载': 'Download',
    '改名': 'Rename',
    '改名文件': 'Rename file',
    '文本': 'Text',
    '上传时间': 'Upload Time',
    '操作': 'Actions',
    '动作': 'Action',
    '彻底删除': 'Delete Permanently',
    '浏览服务器本机磁盘目录，支持图片预览、视频观影、音频听音和文本查看。': 'Browse server local folders with image preview, video playback, audio playback, and text viewing.',
    '当前路径：/': 'Current path: /',
    '根目录': 'Root',
    '共享目录': 'Shared Folder',
    '回收站': 'Trash',
    '上一级': 'Up',
    '⊟ 分栏': '⊟ Split',
    '☰ 列表': '☰ List',
    '上传': 'Upload',
    '显示隐藏项': 'Show hidden items',
    '名称': 'Name',
    '类型': 'Type',
    '大小': 'Size',
    '修改时间': 'Modified',
    '移除': 'Remove',
    '目录树': 'Folder Tree',
    '无子目录': 'No subfolders',
    '当前目录没有文件。': 'No files in the current folder.',
    '管理服务运行参数与维护操作。': 'Manage service settings and maintenance actions.',
    '存储路径': 'Storage Path',
    '语言设置': 'Language',
    '刷新模板缓存': 'Refresh Template Cache',
    '本地磁盘访问': 'Local Disk Access',
    '分别控制管理员和普通用户是否可以访问服务器本地磁盘。': 'Control whether administrators and regular users can access the server local disk separately.',
    '允许管理员访问本地磁盘': 'Allow administrators to access local disk',
    '允许普通用户访问本地磁盘': 'Allow regular users to access local disk',
    '保存': 'Save',
    '加载本地磁盘访问设置失败：': 'Failed to load local disk access settings: ',
    '本地磁盘访问设置已保存': 'Local disk access settings saved',
    '保存本地磁盘访问设置失败：': 'Failed to save local disk access settings: ',
    '数据备份路径': 'Data Backup Path',
    '设置后会将虚拟磁盘数据完整镜像到备份目录。': 'When set, virtual disk data is fully mirrored to the backup directory.',
    '上传数据自动同步：已启用': 'Upload auto-sync: enabled',
    '上传数据自动同步：已禁用': 'Upload auto-sync: disabled',
    '启用上传自动同步': 'Enable upload auto-sync',
    '禁用上传自动同步': 'Disable upload auto-sync',
    '上传数据自动同步已启用': 'Upload auto-sync enabled',
    '上传数据自动同步已禁用': 'Upload auto-sync disabled',
    '切换上传数据自动同步失败：': 'Failed to toggle upload auto-sync: ',
    '备份路径': 'Backup path',
    '添加备份': 'Add Backup',
    '速度：': 'Speed: ',
    '开始上传...': 'Starting upload...',
    '继续上传...': 'Continuing upload...',
    '上传已暂停': 'Upload paused',
    '上传已取消': 'Upload cancelled',
    '上传已暂停，点击继续将重新开始上传。': 'Upload paused. Click Continue to restart the upload.',
    '暂停上传失败：': 'Failed to pause upload: ',
    '继续上传失败：': 'Failed to continue upload: ',
    '取消上传失败：': 'Failed to cancel upload: ',
    '同步': 'Sync',
    '同步备份': 'Sync Backups',
    '正在同步备份': 'Syncing backups',
    '开始同步': 'Start Sync',
    '将添加为备份路径。需要立即拷贝主存储数据时，请点击主存储旁边的同步按钮。': 'This will add the backup path. To copy primary storage data now, click the Sync button next to the primary storage path.',
    '将把主存储上的数据拷贝到所有启用的备份存储。遇到同名但内容不同的文件时会暂停并提示。': 'Primary storage data will be copied to all enabled backup storage paths. If a same-name file has different content, syncing will pause and ask what to do.',
    '没有启用的备份路径': 'No enabled backup paths',
    '正在提交备份同步任务...': 'Submitting backup sync task...',
    '备份同步完成': 'Backup sync completed',
    '备份同步失败': 'Backup sync failed',
    '备份同步失败：': 'Backup sync failed: ',
    '备份文件冲突': 'Backup File Conflict',
    '备份目录下已存在同名但内容不同的文件：': 'The backup directory already has a same-name file with different content: ',
    '跳过': 'Skip',
    '记住跳过': 'Always Skip',
    '停止备份': 'Stop Backup',
    '备份已暂停': 'Backup paused',
    '备份已停止': 'Backup stopped',
    '继续备份': 'Continue Backup',
    '正在处理备份文件冲突：': 'Handling backup file conflict: ',
    '正在处理同名文件(相同跳过)：': 'Handling same-name file (same content skipped): ',
    '正在备份：': 'Backing up: ',
    '正在准备备份同步': 'Preparing backup sync',
    '等待备份同步': 'Waiting for backup sync',
    '正在清理备份目录': 'Cleaning backup directory',
    '发现同名但内容不同的文件': 'Same-name file has different content',
    '暂停备份失败：': 'Failed to pause backup: ',
    '继续备份失败：': 'Failed to continue backup: ',
    '停止备份失败：': 'Failed to stop backup: ',
    '确定要停止本次备份同步吗？': 'Stop this backup sync?',
    '设为主存储': 'Make Primary',
    '切换主备目录': 'Swap Primary and Backup',
    '启用': 'Enable',
    '禁用': 'Disable',
    '备份路径状态已更新：': 'Backup path state updated: ',
    '切换备份路径状态失败：': 'Failed to toggle backup path state: ',
    '暂无备份路径': 'No backup paths',
    '移除备份路径': 'Remove Backup Path',
    '已移除备份路径：': 'Backup path removed: ',
    '移除备份路径失败：': 'Failed to remove backup path: ',
    '移除后不会删除该备份目录，只是不再同步新的虚拟磁盘数据。': 'Removing this backup will not delete its directory; new virtual disk data will no longer be synced there.',
    '备份路径不能为空': 'Backup path cannot be empty',
    '确认添加备份路径': 'Confirm Backup Path',
    '将立即把当前虚拟磁盘数据完整同步到备份目录。': 'The current virtual disk data will be fully synced to the backup directory now.',
    '备份路径已添加：': 'Backup path added: ',
    '设置备份路径失败：': 'Failed to set backup path: ',
    '已取消备份路径': 'Backup path disabled',
    '取消备份路径失败：': 'Failed to disable backup path: ',
    '请先设置备份路径': 'Set a backup path first',
    '将把当前备份目录设为主存储目录，并把原主存储目录设为新的备份目录。': 'The current backup directory will become primary storage, and the old primary storage will become the new backup directory.',
    '确认切换': 'Confirm Swap',
    '主备目录已切换：': 'Primary and backup directories swapped: ',
    '切换主备目录失败：': 'Failed to swap primary and backup directories: ',
    '当前虚拟磁盘文件保存的位置。': 'The current location where virtual disk files are stored.',
    '设置': 'Set',
    '正在移动存储文件': 'Moving stored files',
    '等待开始...': 'Waiting to start...',
    '设置界面显示语言，保存后会切换到对应语言版本。': 'Set the interface language. After applying, the matching language version will open.',
    '简体中文': 'Simplified Chinese',
    '应用': 'Apply',
    '新建标签': 'Create Tag',
    '请输入标签名称。': 'Enter a tag name.',
    '标签名称': 'Tag Name',
    '取消': 'Cancel',
    '确认': 'Confirm',
    '解锁目录': 'Unlock Folder',
    '请输入目录锁密码。': 'Enter the folder lock password.',
    '锁密码': 'Lock Password',
    '确认操作': 'Confirm Action',
    '请确认是否继续。': 'Please confirm whether to continue.',
    '选择存储路径': 'Choose Storage Path',
    '请选择一个本地目录作为新的存储路径。': 'Choose a local folder as the new storage path.',
    '当前没有可选择的目录。': 'No folders available.',
    '确定': 'OK',
    '上传到虚拟磁盘': 'Upload to Virtual Disk',
    '选择远程目标目录。': 'Choose the remote target folder.',
    '当前没有文件夹。': 'No folders available.',
    '上传中': 'Uploading',
    '正在上传本地文件到虚拟磁盘。': 'Uploading local files to the virtual disk.',
    '关闭': 'Close',
    '视频': 'Video',
    '音频': 'Audio',
    '图片': 'Images',
    '仅视频': 'Video only',
    '仅音频': 'Audio only',
    '仅图片': 'Images only',
    '观影': 'Play',
    '听音': 'Listen',
    '查看': 'View',
    '文件': 'File',
    '随机播放': 'Shuffle',
    '顺序播放': 'Sequential',
    '循环播放': 'Loop',
    '最大化': 'Maximize',
    '最小化': 'Minimize',
    '复原': 'Restore',
    '向左旋转90度': 'Rotate left 90 degrees',
    '向右旋转90度': 'Rotate right 90 degrees',
    '上一张': 'Previous image',
    '下一张': 'Next image',
    '剪切': 'Crop',
    '应用剪切': 'Apply Crop',
    '取消剪切': 'Cancel Crop',
    '等比例放大': 'Zoom in',
    '等比例缩小': 'Zoom out',
    '当前图像尺寸': 'Current image size',
    '图片宽度': 'Image width',
    '图片高度': 'Image height',
    '下载到本地': 'Download',
    '保存到服务': 'Save to service',
    '收起左侧栏': 'Collapse sidebar',
    '展开左侧栏': 'Expand sidebar',
    '浏览本地目录': 'Browse local folders',
    '当前用户目录': 'Current user folder',
    '分栏视图': 'Split view',
    '列表视图': 'List view',
    '全选当前列表文件': 'Select all files in the current list',
    '全选当前目录文件': 'Select all files in the current folder',
    '给选中文件加标签': 'Add tags to selected files',
    '移至回收站': 'Move to Trash',
    '展开或创建文件标签树': 'Expand or create file tag tree',
    '展开或收起目录': 'Expand or collapse folder'
    ,'点击重新加锁': 'Click to lock again'
    ,'点击解锁': 'Click to unlock'
    ,'已加锁': 'Locked'
    ,'加锁': 'Lock'
    ,'解锁': 'Unlock'
    ,'去锁': 'Remove Lock'
    ,'使用本地播放器播放': 'Open with Local Player'
    ,'选择本地播放器': 'Choose Local Player'
    ,'新建子目录': 'Create Subfolder'
    ,'改名': 'Rename'
    ,'目录已重新加锁：': 'Folder relocked: '
    ,'加锁目录': 'Lock Folder'
    ,'请输入目录「': 'Enter the lock password for folder "'
    ,'」的锁密码。': '".'
    ,'请为目录「': 'Set a lock password for folder "'
    ,'」设置锁密码。加锁后需要输入密码才能访问。': '". A password will be required to access it after locking.'
    ,'请输入新锁密码': 'Enter a new lock password'
    ,'加锁失败，请重新输入密码。': 'Lock failed. Please re-enter the password.'
    ,'加锁失败：密码错误或验证失败': 'Lock failed: incorrect password or verification failed'
    ,'目录已加锁：': 'Folder locked: '
    ,'目录已解锁（当前会话）：': 'Folder unlocked (current session): '
    ,'去锁目录': 'Remove Folder Lock'
    ,'」的锁密码。验证成功后会永久移除该目录锁。': '". After verification succeeds, the folder lock will be removed permanently.'
    ,'密码错误或去锁失败，请重新输入。': 'Incorrect password or unlock removal failed. Please try again.'
    ,'去锁失败：密码错误或验证失败': 'Remove lock failed: incorrect password or verification failed'
    ,'目录已去锁：': 'Folder lock removed: '
    ,'加锁文件': 'Lock File'
    ,'请为文件「': 'Set a lock password for file "'
    ,'」设置锁密码。': '".'
    ,'文件已加锁：': 'File locked: '
    ,'解锁文件': 'Unlock File'
    ,'请输入文件「': 'Enter the lock password for file "'
    ,'文件已解锁（当前会话）：': 'File unlocked (current session): '
    ,'文件已重新加锁：': 'File relocked: '
    ,'去锁文件': 'Remove File Lock'
    ,'」的锁密码。验证成功后会永久移除该文件锁。': '". After verification succeeds, the file lock will be removed permanently.'
    ,'文件已去锁：': 'File lock removed: '
    ,'已调用本地播放器：': 'Local player launched: '
    ,'已打开本地播放器选择窗口：': 'Local player chooser opened: '
    ,'加锁本地目录': 'Lock Local Folder'
    ,'请为本地目录「': 'Set a lock password for local folder "'
    ,'本地目录已加锁：': 'Local folder locked: '
    ,'解锁本地目录': 'Unlock Local Folder'
    ,'请输入本地目录「': 'Enter the lock password for local folder "'
    ,'本地目录已解锁（当前会话）：': 'Local folder unlocked (current session): '
    ,'本地目录已重新加锁：': 'Local folder relocked: '
    ,'去锁本地目录': 'Remove Local Folder Lock'
    ,'」的锁密码。验证成功后会永久移除该目录锁。': '". After verification succeeds, the directory lock will be removed permanently.'
    ,'本地目录已去锁：': 'Local folder lock removed: '
    ,'移入回收站': 'Move to Trash'
    ,'确认将选中的 ': 'Move the selected '
    ,' 个本地目录移至回收站？': ' local folders to Trash?'
    ,' 个文件夹及其全部内容移入回收站？': ' folders and all their contents to Trash?'
    ,'加载元数据超时': 'Loading metadata timed out'
    ,'浏览器不支持该视频编码或容器': 'The browser does not support this video codec or container'
    ,'无法解析视频': 'Unable to parse video'
    ,'音频编码 ': 'Audio codec '
    ,' 浏览器不支持': ' is not supported by the browser'
    ,'播放方式': 'Playback Mode'
    ,'该标签下没有可播放的音频文件': 'There are no playable audio files under this tag'
    ,'音频播放': 'Audio Playback'
    ,'：': ': '
    ,'音频标签': 'Audio Tag'
    ,'共 ': ''
    ,' 个音频文件': ' audio files'
    ,'收起虚拟磁盘': 'Collapse Virtual Disk'
    ,'展开虚拟磁盘': 'Expand Virtual Disk'
    ,'该标签下没有音频文件': 'There are no audio files under this tag'
    ,'请输入标签名称': 'Enter tag name'
    ,'请输入锁密码': 'Enter lock password'
    ,'密码错误或验证失败，请重新输入。': 'Incorrect password or verification failed. Please try again.'
    ,'解锁失败：密码错误或验证失败': 'Unlock failed: incorrect password or verification failed'
    ,'新增子标签': 'Add Subtag'
    ,'删除标签': 'Delete Tag'
    ,'新增一级标签': 'Add Root Tag'
    ,'给 ': 'Add tags to '
    ,' 个文件加入标签': ' files'
    ,'加入标签': 'Add Tag'
    ,'新建子标签并加入': 'Create Subtag and Add'
    ,'创建在「': 'Create under "'
    ,'」下，创建后会立即把当前文件加入该标签。': '". After creation, the current file will be added to it.'
    ,'已创建子标签并加入文件': 'Created subtag and added file'
    ,'已创建子标签并将 ': 'Created subtag and added '
    ,'当前没有标签': 'There are currently no tags'
    ,'标签名称不能为空': 'Tag name cannot be empty'
    ,'标签已改名：': 'Tag renamed: '
    ,'标签改名失败：': 'Tag rename failed: '
    ,'标签不存在，可能已被删除': 'The tag does not exist and may have been deleted'
    ,'保留标签不能加锁': 'Reserved tags cannot be locked'
    ,'加锁标签': 'Lock Tag'
    ,'请为标签「': 'Set a lock password for tag "'
    ,'」设置锁密码。加锁后需要输入密码才能查看该标签下的文件。': '". A password will be required to view files under this tag after locking.'
    ,'标签已加锁：': 'Tag locked: '
    ,'解锁标签': 'Unlock Tag'
    ,'请输入标签「': 'Enter the lock password for tag "'
    ,'标签已解锁（当前会话）：': 'Tag unlocked (current session): '
    ,'标签已重新加锁：': 'Tag relocked: '
    ,'去锁标签': 'Remove Tag Lock'
    ,'」的锁密码。验证成功后会永久移除该标签锁。': '". After verification succeeds, the tag lock will be removed permanently.'
    ,'标签已去锁：': 'Tag lock removed: '
    ,'视频标签及其子标签只能引用视频文件（mp4/avi/mkv/rm/rmvb/mov/wmv/mpg/mpeg）': 'Video tags and their subtags can only reference video files (mp4/avi/mkv/rm/rmvb/mov/wmv/mpg/mpeg)'
    ,'音频标签及其子标签只能引用音频文件（mp3/m4a/aac/wav/ogg/flac）': 'Audio tags and their subtags can only reference audio files (mp3/m4a/aac/wav/ogg/flac)'
    ,'图片标签及其子标签只能引用图片文件（png/jpg/jpeg/gif/heic/heif）': 'Image tags and their subtags can only reference image files (png/jpg/jpeg/gif/heic/heif)'
    ,'创建标签失败': 'Failed to create tag'
    ,'请选择要引用的文件': 'Please select a file to reference'
    ,'检测到以下视频建议转码（需你确认后才会开始）：': 'The following videos are recommended for transcoding and will start only after your confirmation:'
    ,'检测到以下视频为了兼容浏览器建议转换为MP4，请确认是否转换：': 'The following videos should be converted to MP4 for browser compatibility. Please confirm whether to convert:'
    ,'检测到以下视频为了兼容浏览器建议进行兼容处理，请选择是否处理：': 'The following videos need browser compatibility handling. Please choose whether to process them:'
    ,'RM/RMVB视频为旧格式，为了兼容浏览器播放，建议转换为MP4': 'RM/RMVB is a legacy video format. Convert it to MP4 for better browser playback compatibility.'
    ,'RM/RMVB/MOV/WMV/MPG/MPEG格式浏览器兼容性较差，可转换为MP4后播放。': 'RM/RMVB/MOV/WMV/MPG/MPEG has poor browser compatibility. Convert it to MP4 before playback.'
    ,'本地磁盘RM/RMVB/AVI/MOV/WMV/MPG/MPEG格式可转换为MP4，输出文件会保存在源文件相同目录。': 'Local RM/RMVB/AVI/MOV/WMV/MPG/MPEG videos can be converted to MP4. The output file will be saved in the same folder as the source file.'
    ,'边转边看': 'Watch While Converting'
    ,'边转边看中': 'Streaming Conversion'
    ,'等待边转边看': 'Waiting to stream'
    ,'边转边看准备中...': 'Preparing streaming conversion...'
    ,'边转边看进度 ': 'Streaming conversion '
    ,'边转边看中...': 'Streaming conversion...'
    ,'边转边看完成 100%': 'Streaming conversion complete 100%'
    ,'边转边看失败': 'Streaming conversion failed'
    ,'转换为MP4': 'Convert to MP4'
    ,'当前声音由拆分出的独立音频文件同步播放。由于视频文件本身没有音轨，播放器里的音量图标可能显示为禁用，但不影响实际出声。': 'Audio is currently played by a split sidecar audio file synchronized with the video. Because the video file itself has no audio track, the player volume icon may appear disabled, but actual sound playback is not affected.'
    ,'处理视频兼容性': 'Handle Video Compatibility'
    ,'转换任务已启动': 'Conversion Started'
    ,'上传完成，检测到 ': 'Upload complete. Detected '
    ,' 个视频建议转换为MP4以兼容浏览器播放。是否现在转换？': ' video(s) recommended for MP4 conversion for browser playback compatibility. Convert now?'
    ,' 个视频需要进行兼容处理。其中音频不兼容的视频可选择拆分视频并转音频或音视频都转。是否继续？': ' video(s) need compatibility handling. For videos with unsupported audio, you can choose split-video-plus-audio-transcode or full audio/video conversion. Continue?'
    ,'继续处理': 'Continue'
    ,'保持原文件': 'Keep Original'
    ,'已保留原视频文件，稍后播放时仍可转换为MP4。': 'Original video files kept. You can still convert them to MP4 later when playing.'
    ,'已保留原视频文件，稍后仍可进行兼容处理。': 'Original video files kept. You can still handle compatibility later.'
    ,'浏览器兼容性不足': 'Insufficient browser compatibility'
    ,'原因：': 'Reason: '
    ,'确认转码': 'Confirm Transcode'
    ,'只转音频': 'Audio Only'
    ,'拆分视频并转音频': 'Split Video and Transcode Audio'
    ,'拆分并转音频': 'Split and Transcode Audio'
    ,'提示：仅转音轨（拆分并转音频）速度更快，画面无需重新编码。': 'Tip: Transcoding audio only (split and transcode audio) is faster because the video stream does not need to be re-encoded.'
    ,'转码完成后请关闭并重新打开播放窗口。': 'After transcoding completes, close and reopen the playback window.'
    ,'正在启动转码任务...': 'Starting transcoding task...'
    ,'浏览器无法直接播放此视频': 'This video cannot be played directly in the browser'
    ,'浏览器无法播放该视频，可转码为 MP4 后再试': 'This video cannot be played in the browser. Try converting it to MP4 first.'
    ,' 浏览器不支持，可转码后播放': ' is not supported by the browser. Transcode the file to play it.'
    ,'音视频都转': 'Transcode Audio and Video'
    ,'取消转码': 'Cancel Transcode'
    ,'不转换': 'Do Not Convert'
    ,'等待确认': 'Waiting for confirmation'
    ,'状态：': 'Status: '
    ,'后台转码中': 'Transcoding in background'
    ,'转码中': 'Transcoding'
    ,'已完成': 'Completed'
    ,'状态：已完成，输出文件 ': 'Status: Completed, output file '
    ,'已取消': 'Cancelled'
    ,'失败': 'Failed'
    ,'状态：已取消': 'Status: Cancelled'
    ,'状态：正在请求后台启动转码任务（只转音频）': 'Status: Requesting background task start (audio only)'
    ,'状态：正在请求后台启动转码任务（拆分视频并转音频）': 'Status: Requesting background task start (split video and transcode audio)'
    ,'状态：正在请求后台启动转码任务（音视频都转）': 'Status: Requesting background task start (audio and video)'
    ,'状态：失败，': 'Status: Failed, '
    ,'未知错误': 'Unknown error'
    ,'进度查询失败': 'Failed to query progress'
    ,'状态：进度查询失败，': 'Status: Failed to query progress, '
    ,'状态：找不到任务号，无法取消': 'Status: Task id not found, cannot cancel'
    ,'取消中': 'Cancelling'
    ,'取消中...': 'Cancelling...'
    ,'状态：已发送取消请求，等待后台停止': 'Status: Cancel request sent, waiting for backend to stop'
    ,'状态：取消失败，': 'Status: Cancel failed, '
    ,'任务创建中...': 'Creating task...'
    ,'状态：正在请求后台启动转码任务': 'Status: Requesting backend to start transcoding task'
    ,'无需转码': 'No transcoding needed'
    ,'状态：文件已经可直接播放': 'Status: File is already directly playable'
    ,'状态：后台任务已启动，任务号 ': 'Status: Background task started, task id '
    ,'已启动': 'Started'
    ,'失败：': 'Failed: '
    ,'上传中 ': 'Uploading '
    ,'准备上传...': 'Preparing upload...'
    ,'当前没有文件夹。': 'There are currently no folders.'
    ,'文件夹名称不能为空': 'Folder name cannot be empty'
    ,'文件夹已改名：': 'Folder renamed: '
    ,'文件夹改名失败：': 'Folder rename failed: '
    ,'请输入要创建在「': 'Enter the name of the subfolder to create under "'
    ,'」下的子目录名称。': '".'
    ,'目录名称': 'Folder Name'
    ,'请输入目录名称': 'Enter folder name'
    ,'已创建文件夹：': 'Folder created: '
    ,'彻底删除目录': 'Permanently Delete Folder'
    ,'确认彻底删除回收站中的目录「': 'Permanently delete the folder "'
    ,'」及其全部内容？此操作不可恢复。': '" and all its contents from Trash? This action cannot be undone.'
    ,'彻底删除': 'Permanently Delete'
    ,'回收站目录已彻底删除': 'Trash folder permanently deleted'
    ,'删除目录': 'Delete Folder'
    ,'确认将目录「': 'Move folder "'
    ,'」及其全部内容移入回收站？': '" and all its contents to Trash?'
    ,'文件夹已移入回收站': 'Folder moved to Trash'
    ,'请先选择要上传的本地文件或文件夹': 'Please select local files or folders to upload first'
    ,'加载远程目录失败：': 'Failed to load remote folder: '
    ,'完成': 'Completed'
    ,'上传中': 'Uploading'
    ,'等待': 'Waiting'
    ,'上传完成 100%': 'Upload complete 100%'
    ,'上传失败': 'Upload failed'
    ,'缺少上传任务编号': 'Missing upload task id'
    ,'已上传 ': 'Uploaded '
    ,' 个本地文件到虚拟磁盘': ' local files to virtual disk'
    ,'上传失败：': 'Upload failed: '
    ,'上传本地文件失败：': 'Uploading local files failed: '
    ,'当前标签': 'Current Tag'
    ,'当前视图：标签：': 'Current view: Tag: '
    ,' / 范围：标签内全部文件': ' / Scope: all files under tag'
    ,'当前视图：目录：': 'Current view: Folder: '
    ,' / 范围：全部文件': ' / Scope: all files'
    ,'当前标签文件': 'Current Tag Files'
    ,'当前目录文件': 'Current Folder Files'
    ,'文件/文件夹': 'Files/Folders'
    ,'文件': 'File'
    ,'移除': 'Remove'
    ,'文件已改名：': 'File renamed: '
    ,'文件改名失败：': 'File rename failed: '
    ,'文件摘要失败：未找到文件': 'File summary failed: file not found'
    ,'确认将文件『': 'Remove file "'
    ,'』从当前标签中移除？此操作只解除标签引用，不会删除文件。': '" from the current tag? This only removes the tag reference and will not delete the file.'
    ,'移除引用失败：关联不存在': 'Remove reference failed: link does not exist'
    ,'已移除标签引用：': 'Tag reference removed: '
    ,'确认删除文件：': 'Delete file: '
    ,' ？将先移入回收站。': ' ? It will be moved to Trash first.'
    ,'已移入回收站：': 'Moved to Trash: '
    ,'确认彻底删除': 'Permanently delete '
    ,'：': ': '
    ,' ？此操作不可恢复。': ' ? This action cannot be undone.'
    ,'已彻底删除': 'Permanently deleted '
    ,'恢复': 'Restore'
    ,'批量从当前标签移除': 'Remove selected items from current tag'
    ,'批量恢复文件到原路径': 'Restore selected files to original paths'
    ,'批量删除文件（移入回收站）': 'Delete selected files (move to Trash)'
    ,'批量彻底删除文件/文件夹（仅回收站）': 'Permanently delete selected files/folders (Trash only)'
    ,'给选中的 ': 'Add tags to selected '
    ,' 个文件加标签': ' files'
    ,'当前标签下没有文件。': 'There are no files under the current tag.'
    ,'当前目录没有文件。': 'There are no files in the current folder.'
    ,'预览': 'Preview'
    ,'观影': 'Watch'
    ,'听音': 'Listen'
    ,'查看': 'View'
    ,'选择': 'Select '
    ,'目录 ': 'folder '
    ,'文件 ': 'file '
    ,'关闭': 'Close'
    ,'语言设置已保存': 'Language setting saved'
    ,'字体大小': 'Font Size'
    ,'设置界面文字大小，保存后立即生效。': 'Adjust interface text size. Changes apply immediately after saving.'
    ,'小号': 'Small'
    ,'中号': 'Medium'
    ,'大号': 'Large'
    ,'字体设置已保存': 'Font size setting saved'
    ,'保存语言设置失败：': 'Failed to save language setting: '
    ,'保存字体设置失败：': 'Failed to save font size setting: '
    ,'加载存储路径失败：': 'Failed to load storage path: '
    ,'移动完成': 'Move completed'
    ,'存储路径迁移失败': 'Storage path migration failed'
    ,'存储路径不能为空': 'Storage path cannot be empty'
    ,'存储路径未改变': 'Storage path unchanged'
    ,'确认修改存储路径': 'Confirm Storage Path Change'
    ,'根路径': 'Root Path'
    ,'Home路径': 'Home Path'
    ,'加载本地目录树失败：': 'Failed to load local directory tree: '
    ,'是否将当前存储路径下的文件移动到目标目录？': 'Move files from the current storage path to the target directory?'
    ,'是否将当前存储路径下的文件移动到目标目录？选择“否”将不移动文件，也不会修改存储路径。': 'Move files from the current storage path to the target directory? Choosing "No" will neither move files nor change the storage path.'
    ,'是否将当前存储路径下的文件移动到目标目录？选择“否”将只修改存储路径，不迁移文件。': 'Move files from the current storage path to the target directory? Choosing "No" changes only the storage path and does not migrate files.'
    ,'是，开始移动': 'Yes, start moving'
    ,'否': 'No'
    ,'否，只修改路径': 'No, change path only'
    ,'已取消，存储路径未修改': 'Cancelled, storage path unchanged'
    ,'存储路径已修改，未迁移文件：': 'Storage path changed without migrating files: '
    ,'只修改存储路径失败：': 'Failed to change storage path only: '
    ,'正在提交移动任务...': 'Submitting move task...'
    ,'目标文件已存在': 'Target File Exists'
    ,'目标目录下已存在同名文件：': 'The target folder already has a file with the same name: '
    ,'覆盖': 'Overwrite'
    ,'不覆盖': 'Do Not Overwrite'
    ,'记住覆盖': 'Always Overwrite'
    ,'记住不覆盖': 'Always Skip'
    ,'取消迁移': 'Cancel Migration'
    ,'等待处理同名文件...': 'Waiting for duplicate file decision...'
    ,'正在处理同名文件：': 'Handling duplicate file: '
    ,'正在处理同名文件(覆盖)：': 'Handling duplicate file (overwrite): '
    ,'正在处理同名文件(跳过)：': 'Handling duplicate file (skip): '
    ,'正在拷贝：': 'Copying: '
    ,'正在准备迁移': 'Preparing migration'
    ,'继续迁移': 'Resume migration'
    ,'发现同名文件': 'Duplicate file found'
    ,'迁移完成': 'Migration completed'
    ,'迁移已取消': 'Migration cancelled'
    ,'迁移已暂停': 'Migration paused'
    ,'暂停': 'Pause'
    ,'继续': 'Resume'
    ,'确认取消': 'Confirm Cancel'
    ,'确定要取消本次迁移吗？': 'Cancel this migration?'
    ,'暂停迁移失败：': 'Failed to pause migration: '
    ,'继续迁移失败：': 'Failed to resume migration: '
    ,'取消迁移失败：': 'Failed to cancel migration: '
    ,'存储路径已修改：': 'Storage path changed: '
    ,'是否删除旧数据？': 'Delete Old Data?'
    ,'迁移已完成，是否删除原存储路径下的旧数据？备份目录 .backup 会保留。': 'Migration is complete. Delete old data under the previous storage path? The .backup directory will be kept.'
    ,'删除旧数据': 'Delete Old Data'
    ,'不删除': 'Do Not Delete'
    ,'已保留旧数据：': 'Old data kept: '
    ,'已删除旧数据：': 'Old data deleted: '
    ,'上传完成：成功保存 ': 'Upload completed: successfully saved '
    ,' 个文件': ' files'
    ,'已打开系统回收站': 'System Trash opened'
    ,'打开系统回收站失败：': 'Failed to open system Trash: '
    ,'拷贝': 'Copy'
    ,'粘贴': 'Paste'
    ,'上传': 'Upload'
    ,'已拷贝 ': 'Copied '
    ,' 个本地文件': ' local files'
    ,' 个本地目录': ' local folders'
    ,' 个虚拟磁盘文件': ' virtual disk files'
    ,'打开上传目标选择失败：': 'Failed to open upload target picker: '
    ,'已拷贝本地文件路径：': 'Copied local file path: '
    ,'已拷贝本地目录路径：': 'Copied local folder path: '
    ,'已拷贝远程文件路径：': 'Copied remote file path: '
    ,'已拷贝远程目录路径：': 'Copied remote folder path: '
    ,'没有可粘贴的本地文件或目录': 'No local file or folder to paste'
    ,'没有可粘贴的远程文件或目录': 'No remote file or folder to paste'
    ,'不能将目录粘贴到自身或其子目录中': 'Cannot paste a folder into itself or one of its subfolders'
    ,'目标目录下已存在同名文件或目录，是否覆盖？': 'The target folder already has an item with the same name. Overwrite it?'
    ,'已取消粘贴': 'Paste cancelled'
    ,'已粘贴到：': 'Pasted to: '
    ,'已粘贴 ': 'Pasted '
    ,' 个项目到：': ' items to: '
    ,'粘贴中': 'Pasting'
    ,'正在将虚拟磁盘文件或目录粘贴到目标目录。': 'Pasting virtual disk files or folders to the target folder.'
    ,'正在将本地磁盘文件或目录粘贴到目标目录。': 'Pasting local disk files or folders to the target folder.'
    ,'准备粘贴...': 'Preparing paste...'
    ,'正在取消粘贴...': 'Cancelling paste...'
    ,'取消粘贴失败：': 'Failed to cancel paste: '
    ,'缺少拷贝任务编号': 'Missing copy task id'
    ,'粘贴完成 100%': 'Paste completed 100%'
    ,'粘贴已取消': 'Paste cancelled'
    ,'粘贴失败': 'Paste failed'
    ,'请先选择要加标签的本地文件': 'Please select local files to tag first'
    ,'打开标签选择失败：': 'Failed to open tag picker: '
    ,'本地目录锁操作失败：': 'Local folder lock operation failed: '
    ,'文件锁操作失败：': 'File lock operation failed: '
    ,'解锁本地目录失败：': 'Failed to unlock local folder: '
    ,'请输入新建子目录名称': 'Enter new subfolder name'
    ,'子目录名称不能为空': 'Subfolder name cannot be empty'
    ,'子目录已创建：': 'Subfolder created: '
    ,'创建子目录失败：': 'Failed to create subfolder: '
    ,'确认删除本地目录：': 'Delete local folder: '
    ,' ？仅允许删除空目录。': ' ? Only empty folders can be deleted.'
    ,'确认删除本地文件：': 'Delete local file: '
    ,' ？': ' ?'
    ,'本地目录已删除：': 'Local folder deleted: '
    ,'请输入新的目录名称': 'Enter the new folder name'
    ,'本地目录已改名：': 'Local folder renamed: '
    ,'确认将本地目录移至回收站：': 'Move local folder to Trash: '
    ,'本地目录已移至回收站：': 'Local folder moved to Trash: '
    ,'已移除 ': 'Removed '
    ,' 个本地目录到回收站': ' local folders to Trash'
    ,'本地文件已删除：': 'Local file deleted: '
    ,'删除失败：': 'Delete failed: '
    ,'已批量': 'Bulk '
    ,'在处理 ': ' failed after processing '
    ,' 个文件后失败：': ' files: '
    ,'批量': 'Bulk '
    ,'请先选择要加标签的文件': 'Please select files to tag first'
    ,'确认彻底删除选中的 ': 'Permanently delete the selected '
    ,' 个': ' '
    ,'？其中的文件夹会连同其全部内容一起删除，此操作不可恢复。': '? Folders will be deleted together with all their contents. This action cannot be undone.'
    ,' 个文件？此操作不可恢复。': ' files? This action cannot be undone.'
    ,'已批量彻底删除 ': 'Bulk permanently deleted '
    ,'批量彻底删除在处理 ': 'Bulk permanent delete failed after processing '
    ,'批量彻底删除失败：': 'Bulk permanent delete failed: '
    ,'创建文件夹失败：': 'Failed to create folder: '
    ,'删除文件夹失败：': 'Failed to delete folder: '
    ,'恢复文件夹失败：': 'Failed to restore folder: '
    ,'重新加锁失败：': 'Failed to relock: '
    ,'移入回收站失败：': 'Move to Trash failed: '
    ,'移动文件夹失败：': 'Move folder failed: '
    ,'移动文件失败：': 'Move file failed: '
    ,'新建一级标签': 'Create Root Tag'
    ,'标签会显示在左侧树的第一层。': 'The tag will appear on the first level of the left tree.'
    ,'请输入一级标签名称': 'Enter root tag name'
    ,'创建标签失败：': 'Failed to create tag: '
    ,'一级标签已创建': 'Root tag created'
    ,'标签锁操作失败：': 'Tag lock operation failed: '
    ,'加载标签文件失败：': 'Failed to load tag files: '
    ,'受限一级标签不能删除': 'Restricted root tags cannot be deleted'
    ,'确认删除该标签节点及其子节点？仅会删除标签引用关系，不会删除文件。': 'Delete this tag node and its child nodes? Only tag references will be removed, files will not be deleted.'
    ,'删除标签失败：': 'Failed to delete tag: '
    ,'节点不存在': 'Node does not exist'
    ,'标签节点已删除（未删除任何文件）': 'Tag node deleted (no files were deleted)'
    ,'当前节点下最多支持三级标签。': 'A maximum of three tag levels is supported under the current node.'
    ,'请输入子标签名称': 'Enter subtag name'
    ,'创建子标签失败：': 'Failed to create subtag: '
    ,'子标签已创建': 'Subtag created'
    ,'节点不存在，可能已被删除': 'Node does not exist and may have been deleted'
    ,'解引用失败：关联不存在': 'Unbind failed: relation does not exist'
    ,'文件已解引用': 'File unbound'
    ,'拖拽引用失败：': 'Drag-and-drop bind failed: '
    ,'已批量移动 ': 'Moved '
    ,' 个文件到标签': ' files to tag'
    ,'已通过拖拽移动文件到标签': 'File moved to tag by drag and drop'
    ,'加入标签失败：': 'Failed to add tag: '
    ,'已将 ': 'Added '
    ,'文件已加入标签': 'File added to tag'
    ,'目录操作失败：': 'Folder operation failed: '
    ,'验证中...': 'Verifying...'
    ,'打开音频播放列表失败：': 'Failed to open audio playlist: '
    ,'已向左旋转 90 度，点击“保存到服务”写入文件。': 'Rotated left 90 degrees. Click Save to service to write the file.'
    ,'已向右旋转 90 度，点击“保存到服务”写入文件。': 'Rotated right 90 degrees. Click Save to service to write the file.'
  };

  var phraseEntries = Object.keys(textMap)
    .filter(function (key) { return key && key.length > 1; })
    .sort(function (a, b) { return b.length - a.length; })
    .map(function (key) { return [key, textMap[key]]; });

  var patternMap = [
    [/^(\d+) 个文件$/, function (_, n) { return n + (n === '1' ? ' file' : ' files'); }],
    [/^([\d,]+) 字节$/, '$1 bytes'],
    [/^当前视图：目录：(.+) \/ 范围：全部文件$/, 'Current view: Folder: $1 / Scope: all files'],
    [/^当前视图：标签：(.+) \/ 范围：标签内全部文件$/, 'Current view: Tag: $1 / Scope: all tagged files'],
    [/^当前路径：(.+)$/, 'Current path: $1'],
    [/^图片预览：(.+)$/, 'Image Preview: $1'],
    [/^视频播放：(.+)$/, 'Video Playback: $1'],
    [/^音频播放：(.+)$/, 'Audio Playback: $1'],
    [/^文本查看：(.+)$/, 'Text View: $1'],
    [/^状态：(.+)$/, 'Status: $1'],
    [/^原因：(.+)$/, 'Reason: $1'],
    [/^加载(.+)失败：(.+)$/, 'Failed to load $1: $2'],
    [/^(.+)失败：(.+)$/, '$1 failed: $2']
  ];

  function applyPhraseMap(text) {
    var result = String(text == null ? '' : text);
    phraseEntries.forEach(function (entry) {
      if (result.indexOf(entry[0]) >= 0) {
        result = result.split(entry[0]).join(entry[1]);
      }
    });
    return result;
  }

  function translateText(text) {
    var raw = String(text == null ? '' : text);
    var trimmed = raw.trim();
    if (!trimmed) return raw;
    var translated = Object.prototype.hasOwnProperty.call(textMap, raw)
      ? textMap[raw]
      : textMap[trimmed];
    if (!translated) {
      for (var i = 0; i < patternMap.length; i += 1) {
        if (patternMap[i][0].test(trimmed)) {
          translated = trimmed.replace(patternMap[i][0], patternMap[i][1]);
          break;
        }
      }
    }
    if (!translated) {
      translated = applyPhraseMap(trimmed);
    }
    if (translated === trimmed) return raw;
    return raw.replace(trimmed, translated);
  }

  function shouldTranslateValue(el) {
    if (!el || !el.tagName || el.tagName.toUpperCase() !== 'INPUT') {
      return false;
    }
    var type = String(el.getAttribute('type') || 'text').toLowerCase();
    return type === 'button' || type === 'submit' || type === 'reset';
  }

  function setAttrIfChanged(el, name, value) {
    if (el.getAttribute(name) !== value) {
      el.setAttribute(name, value);
    }
  }

  function translateAttributes(el) {
    if (!el || !el.getAttribute) return;
    if (el.hasAttribute('data-i18n-title')) {
      setAttrIfChanged(el, 'title', translateText(el.getAttribute('data-i18n-title') || ''));
    }
    if (el.hasAttribute('data-i18n-aria-label')) {
      setAttrIfChanged(el, 'aria-label', translateText(el.getAttribute('data-i18n-aria-label') || ''));
    }
    if (el.hasAttribute('data-i18n-placeholder')) {
      setAttrIfChanged(el, 'placeholder', translateText(el.getAttribute('data-i18n-placeholder') || ''));
    }
    ['title', 'aria-label', 'alt', 'placeholder'].forEach(function (name) {
      if (!el.hasAttribute(name)) return;
      var value = el.getAttribute(name);
      var next = translateText(value);
      if (next !== value) setAttrIfChanged(el, name, next);
    });
    if (shouldTranslateValue(el) && el.hasAttribute('value')) {
      var value = el.getAttribute('value');
      var next = translateText(value);
      if (next !== value) setAttrIfChanged(el, 'value', next);
    }
  }

  function translateNode(node) {
    if (!node) return;
    if (node.nodeType === 3) {
      var next = translateText(node.nodeValue);
      if (next !== node.nodeValue) node.nodeValue = next;
      return;
    }
    if (node.nodeType !== 1) return;
    if (/^(SCRIPT|STYLE|TEXTAREA)$/i.test(node.tagName)) return;
    if (node.hasAttribute && node.hasAttribute('data-i18n')) {
      var translated = translateText(node.getAttribute('data-i18n') || '');
      if (node.textContent !== translated) {
        node.textContent = translated;
      }
      translateAttributes(node);
      return;
    }
    translateAttributes(node);
    var walker = document.createTreeWalker(node, NodeFilter.SHOW_TEXT, {
      acceptNode: function (textNode) {
        var parent = textNode.parentElement;
        if (!parent || /^(SCRIPT|STYLE|TEXTAREA)$/i.test(parent.tagName)) {
          return NodeFilter.FILTER_REJECT;
        }
        return NodeFilter.FILTER_ACCEPT;
      }
    });
    var textNodes = [];
    while (walker.nextNode()) textNodes.push(walker.currentNode);
    textNodes.forEach(translateNode);
    Array.prototype.forEach.call(node.querySelectorAll('*'), translateAttributes);
  }

  function apply(root) {
    var target = root || document.body || document.documentElement;
    if (target && target.nodeType === 9) {
      target = target.body || target.documentElement;
    }
    translateNode(target);
  }

  function start() {
    document.documentElement.lang = 'en';
    document.title = translateText(document.title);
    apply(document.body || document.documentElement);
    var observer = new MutationObserver(function (mutations) {
      mutations.forEach(function (mutation) {
        if (mutation.type === 'characterData') {
          translateNode(mutation.target);
        } else if (mutation.type === 'attributes') {
          translateAttributes(mutation.target);
        } else {
          Array.prototype.forEach.call(mutation.addedNodes || [], translateNode);
        }
      });
    });
    observer.observe(document.documentElement, {
      childList: true,
      subtree: true,
      characterData: true,
      attributes: true,
      attributeFilter: ['title', 'aria-label', 'alt', 'placeholder', 'value']
    });
  }

  window.WebCoolI18n = {
    lang: 'en',
    dictionary: textMap,
    t: translateText,
    apply: apply
  };

  if (!window.__webcoolNativeConfirm) {
    window.__webcoolNativeConfirm = window.confirm;
    window.confirm = function (message) {
      return window.__webcoolNativeConfirm.call(window, translateText(message));
    };
  }

  if (!window.__webcoolNativePrompt) {
    window.__webcoolNativePrompt = window.prompt;
    window.prompt = function (message, defaultValue) {
      return window.__webcoolNativePrompt.call(window, translateText(message), defaultValue);
    };
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
})();
