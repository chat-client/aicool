(function () {
  var textMap = {
    // Keep explicit entries for shared UI control labels so zh/en stay aligned.
    '最大化': '最大化',
    '最小化': '最小化',
    '复原': '复原',
    '退出': '退出',
    '退出登录': '退出登录',
    '登录': '登录',
    '注册管理员': '注册管理员',
    '首次使用，请创建管理员用户名和密码。': '首次使用，请创建管理员用户名和密码。',
    '请输入用户名和密码。': '请输入用户名和密码。',
    '认证失败': '认证失败',
    '认证状态检查失败': '认证状态检查失败',
    '创建管理员': '创建管理员',
    '用户名': '用户名',
    '密码': '密码',
    '用户管理': '用户管理',
    '管理普通用户账号和密码。': '管理普通用户账号和密码。',
    '普通用户': '普通用户',
    '管理员账号不会在此处修改或删除。': '管理员账号不会在此处修改或删除。',
    '添加用户': '添加用户',
    '账号设置': '账号设置',
    '修改当前登录用户的密码。': '修改当前登录用户的密码。',
    '当前密码': '当前密码',
    '新密码': '新密码',
    '确认新密码': '确认新密码',
    '修改密码': '修改密码',
    '暂无用户': '暂无用户',
    '修改': '修改',
    '删除': '删除',
    '用户已添加：': '用户已添加：',
    '添加用户失败：': '添加用户失败：',
    '请输入新的用户名': '请输入新的用户名',
    '用户名不能为空': '用户名不能为空',
    '请输入新密码；留空则不修改密码': '请输入新密码；留空则不修改密码',
    '用户已更新：': '用户已更新：',
    '修改用户失败：': '修改用户失败：',
    '确认删除用户：': '确认删除用户：',
    '用户已删除：': '用户已删除：',
    '删除用户失败：': '删除用户失败：',
    '请输入当前密码和新密码。': '请输入当前密码和新密码。',
    '两次输入的新密码不一致。': '两次输入的新密码不一致。',
    '密码已修改。': '密码已修改。',
    '修改密码失败：': '修改密码失败：'
  };

  function applyTranslations(root) {
    root = root || document;
    var nodes = root.querySelectorAll('[data-i18n]');
    Array.prototype.forEach.call(nodes, function (node) {
      node.textContent = textMap[node.getAttribute('data-i18n')] || node.getAttribute('data-i18n') || '';
    });
    var placeholders = root.querySelectorAll('[data-i18n-placeholder]');
    Array.prototype.forEach.call(placeholders, function (node) {
      var key = node.getAttribute('data-i18n-placeholder') || '';
      node.setAttribute('placeholder', textMap[key] || key);
    });
    var titles = root.querySelectorAll('[data-i18n-title]');
    Array.prototype.forEach.call(titles, function (node) {
      var key = node.getAttribute('data-i18n-title') || '';
      node.setAttribute('title', textMap[key] || key);
    });
    var aria = root.querySelectorAll('[data-i18n-aria-label]');
    Array.prototype.forEach.call(aria, function (node) {
      var key = node.getAttribute('data-i18n-aria-label') || '';
      node.setAttribute('aria-label', textMap[key] || key);
    });
  }

  window.WebCoolI18n = {
    lang: 'zh',
    dictionary: textMap,
    t: function (text) {
      var key = String(text == null ? '' : text);
      return Object.prototype.hasOwnProperty.call(textMap, key) ? textMap[key] : key;
    },
    apply: applyTranslations
  };
})();
