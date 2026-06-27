import sys

with open('PhotoFrameApp.cpp', 'r') as f:
    code = f.read()

# Patch 1: index_html CSS
old_css = """        .settings summary { cursor: pointer; font-weight: bold; color: #aaa; outline: none; }
        .settings label { display: block; margin-top: 10px; cursor: pointer; color: #ccc; }
        #status { margin-top: 15px; font-weight: bold; min-height: 20px; color: var(--primary); }
    </style>"""

new_css = """        .settings summary { cursor: pointer; font-weight: bold; color: #aaa; outline: none; }
        .settings label { display: block; margin-top: 10px; cursor: pointer; color: #ccc; }
        #status { margin-top: 15px; font-weight: bold; min-height: 20px; color: var(--primary); }
        .tabs { display: flex; background: #1a1a1a; margin-bottom: 20px; border-radius: 8px; overflow: hidden; }
        .tab { flex: 1; text-align: center; padding: 12px; color: #888; text-decoration: none; font-weight: bold; font-size: 14px; transition: 0.3s; }
        .tab.active { background: var(--primary); color: #fff; }
        .tab:hover:not(.active) { background: #333; color: #fff; }
    </style>"""
if old_css in code:
    code = code.replace(old_css, new_css)
else:
    print("Could not find index_html CSS block to patch.")

# Patch 2: index_html HTML
old_html = """    <div class="container">
        <div class="card">"""

new_html = """    <div class="container">
        <div class="tabs">
            <a href="/" class="tab active">Photo Sync</a>
            <a href="/wifi" class="tab">Wi-Fi Settings</a>
        </div>
        <div class="card">"""
if old_html in code:
    code = code.replace(old_html, new_html)
else:
    print("Could not find index_html HTML block to patch.")


# Patch 3: /wifi CSS and HTML
old_wifi_html = """<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Jimny Dashboard — WiFi Setup</title>
<style>
  body{margin:0;background:#0a0a0a;color:#eee;font-family:system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;padding:24px 16px;}
  h1{font-size:1.3rem;color:#ff6a00;margin:0 0 6px;} p{color:#888;font-size:.85rem;margin:0 0 24px;text-align:center;}
  .card{background:#1a1a1a;border:1px solid #333;border-radius:14px;padding:20px;width:100%;max-width:440px;margin-bottom:20px;}
  .card h2{font-size:1rem;color:#ff9500;margin:0 0 14px;}
  ul{list-style:none;margin:0;padding:0;}
  input{width:100%;box-sizing:border-box;background:#111;border:1px solid #444;color:#eee;padding:10px 12px;border-radius:8px;font-size:1rem;margin-bottom:10px;}
  button.primary{width:100%;background:#ff6a00;color:#fff;border:none;padding:12px;border-radius:10px;font-size:1rem;font-weight:600;cursor:pointer;}
  button.primary:hover{background:#ff8c00;}
</style></head><body>
<h1>Jimny Dashboard — WiFi Setup</h1>
<p>Add your home WiFi or phone hotspot. The device will connect automatically when you check for updates.</p>
<div class='card'>
  <h2>Saved Networks</h2>
  <ul id='netlist'>"""

new_wifi_html = """<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Trailmaster Sync — WiFi Setup</title>
<style>
  :root { --primary: #e67e22; --bg: #121212; --card: #1e1e1e; --text: #f5f5f5; }
  body{margin:0;background:var(--bg);color:var(--text);font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;display:flex;flex-direction:column;align-items:center;padding:0;}
  .header { background: #000; width: 100%; padding: 20px 0; text-align: center; border-bottom: 3px solid var(--primary); margin-bottom: 20px; }
  .header h1 { margin: 0; font-size: 24px; letter-spacing: 2px; text-transform: uppercase; display: flex; align-items: center; justify-content: center; gap: 10px; }
  .container { padding: 0 20px 20px 20px; width: 100%; max-width: 440px; box-sizing: border-box; }
  .tabs { display: flex; background: #1a1a1a; margin-bottom: 20px; border-radius: 8px; overflow: hidden; }
  .tab { flex: 1; text-align: center; padding: 12px; color: #888; text-decoration: none; font-weight: bold; font-size: 14px; transition: 0.3s; }
  .tab.active { background: var(--primary); color: #fff; }
  .tab:hover:not(.active) { background: #333; color: #fff; }
  p{color:#aaa;font-size:14px;margin:0 0 20px;text-align:center;}
  .card{background:var(--card);border-radius:12px;padding:25px;width:100%;box-shadow: 0 8px 16px rgba(0,0,0,0.5);box-sizing:border-box;margin-bottom:20px;}
  .card h2{font-size:16px;color:var(--primary);margin:0 0 14px;text-align:left;}
  ul{list-style:none;margin:0;padding:0;text-align:left;}
  input{width:100%;box-sizing:border-box;background:#111;border:1px solid #444;color:#eee;padding:12px;border-radius:8px;font-size:16px;margin-bottom:15px;}
  button.primary{width:100%;background:var(--primary);color:#fff;border:none;padding:15px;border-radius:8px;font-size:16px;font-weight:bold;text-transform:uppercase;cursor:pointer;transition:0.3s;}
  button.primary:hover{background:#d35400;}
</style></head><body>
  <div class="header">
      <h1>
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="var(--primary)" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"></path><circle cx="12" cy="10" r="3"></circle></svg>
          TRAILMASTER
      </h1>
  </div>
  <div class="container">
      <div class="tabs">
          <a href="/" class="tab">Photo Sync</a>
          <a href="/wifi" class="tab active">Wi-Fi Settings</a>
      </div>
      <p>Add your home WiFi or phone hotspot. The device will connect automatically when you check for updates.</p>
      <div class='card'>
        <h2>Saved Networks</h2>
        <ul id='netlist'>"""
if old_wifi_html in code:
    code = code.replace(old_wifi_html, new_wifi_html)
else:
    print("Could not find /wifi HTML block to patch.")

# Remove back button from /wifi
old_back_btn = "<p style='color:#555;font-size:.75rem;'><a href='/' style='color:#555;'>&#8592; Back to Image Upload</a></p>\n</body></html>"
new_back_btn = "  </div>\n</body></html>"
if old_back_btn in code:
    code = code.replace(old_back_btn, new_back_btn)

with open('PhotoFrameApp.cpp', 'w') as f:
    f.write(code)

print("Patch complete.")
