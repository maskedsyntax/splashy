<script>
  import { onMount } from 'svelte';

  let os = $state('linux');

  onMount(() => {
    if (navigator.userAgent.indexOf("Mac") !== -1) {
      os = 'mac';
    }
  });
</script>

<svelte:head>
  <title>Splashy | Advanced Whiteboard</title>
  <meta name="description" content="A lightweight and feature-rich whiteboard application for Linux and macOS, built with GTK3 and Cairo for maximum performance and responsiveness." />
</svelte:head>

<main>
  <section class="hero">
    <div class="container hero-content">
      <img src="/logo.svg" alt="Splashy Logo" class="logo" />
      <h1 class="title">Splashy</h1>
      <p class="subtitle">The lightweight, high-performance whiteboard for your professional notes and quick sketches.</p>
      <div class="hero-actions">
        <a href="#install" class="button">Get Started</a>
        <a href="https://github.com/maskedsyntax/splashy" target="_blank" rel="noopener noreferrer" class="button secondary">View on GitHub</a>
      </div>
    </div>
  </section>

  <section class="features bg-alt">
    <div class="container">
      <h2 class="section-title">Designed for Performance</h2>
      <div class="feature-grid">
        <div class="feature-card">
          <h3>High Performance</h3>
          <p>Native C implementation using GTK3 and Cairo ensures ultra-low latency and maximum responsiveness.</p>
        </div>
        <div class="feature-card">
          <h3>Stylus Support</h3>
          <p>Full pressure sensitivity support for professional styluses, making your handwriting look natural.</p>
        </div>
        <div class="feature-card">
          <h3>Smooth Drawing</h3>
          <p>Advanced midpoint quadratic Bézier interpolation for incredibly fluid and smooth lines.</p>
        </div>
        <div class="feature-card">
          <h3>Infinite Canvas</h3>
          <p>Unlimited workspace with seamless horizontal and vertical scaling and panning.</p>
        </div>
        <div class="feature-card">
          <h3>Layer Management</h3>
          <p>Organize complex illustrations with robust background and foreground layer support.</p>
        </div>
        <div class="feature-card">
          <h3>Export & History</h3>
          <p>Comprehensive undo/redo stack and the ability to save your canvas as high-quality PNG or PDF files.</p>
        </div>
      </div>
    </div>
  </section>

  <section id="install" class="install">
    <div class="container">
      <h2 class="section-title">Installation</h2>
      
      <div class="os-tabs">
        <button class="os-tab {os === 'linux' ? 'active' : ''}" onclick={() => os = 'linux'}>Linux</button>
        <button class="os-tab {os === 'mac' ? 'active' : ''}" onclick={() => os = 'mac'}>macOS</button>
      </div>

      <div class="install-content">
        {#if os === 'linux'}
          <div class="code-block">
            <h4>1. Install Dependencies</h4>
            <pre><code>sudo apt install build-essential libgtk-3-dev libcairo2-dev</code></pre>
            
            <h4>2. Build and Run</h4>
            <pre><code>make
./build/splashy</code></pre>
          </div>
        {:else if os === 'mac'}
          <div class="code-block">
            <h4>1. Install Dependencies via Homebrew</h4>
            <pre><code>brew install gtk+3 cairo pkg-config</code></pre>
            
            <h4>2. Build the App Bundle</h4>
            <pre><code>make macos</code></pre>
            
            <h4>3. Run the App</h4>
            <pre><code>open build/Splashy.app</code></pre>
          </div>
        {/if}
      </div>
    </div>
  </section>
</main>

<footer>
  <div class="container footer-content">
    <p>&copy; {new Date().getFullYear()} maskedsyntax. Licensed under the <a href="https://github.com/maskedsyntax/splashy/blob/master/LICENSE" target="_blank" rel="noopener noreferrer">MIT License</a>.</p>
  </div>
</footer>

<style>
  /* Local styles specific to the landing page */
  .hero {
    padding: 120px 0 80px;
    text-align: center;
  }

  .logo {
    width: 160px;
    height: 160px;
    margin-bottom: 24px;
    filter: drop-shadow(0 20px 40px rgba(0,0,0,0.1));
  }

  @media (prefers-color-scheme: dark) {
    .logo {
      filter: drop-shadow(0 20px 40px rgba(255,255,255,0.05));
    }
  }

  .title {
    font-size: 56px;
    line-height: 1.05;
    margin-bottom: 16px;
  }

  .subtitle {
    font-size: 24px;
    color: var(--text-secondary);
    max-width: 600px;
    margin: 0 auto 40px;
    font-weight: 400;
  }

  .hero-actions {
    display: flex;
    justify-content: center;
    gap: 16px;
  }

  .bg-alt {
    background-color: var(--bg-secondary);
  }

  .features, .install {
    padding: 100px 0;
  }

  .section-title {
    text-align: center;
    font-size: 44px;
    margin-bottom: 60px;
    letter-spacing: -0.02em;
  }

  .feature-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 24px;
  }

  .feature-card {
    background: var(--card-bg);
    padding: 40px;
    border-radius: var(--border-radius);
    box-shadow: 0 8px 30px rgba(0,0,0,0.04);
    border: 1px solid var(--border-color);
    transition: transform 0.3s ease;
  }

  .feature-card:hover {
    transform: translateY(-5px);
  }

  @media (prefers-color-scheme: dark) {
    .feature-card {
      box-shadow: 0 8px 30px rgba(0,0,0,0.2);
    }
  }

  .feature-card h3 {
    font-size: 22px;
    margin-bottom: 12px;
  }

  .feature-card p {
    color: var(--text-secondary);
    font-size: 17px;
    line-height: 1.6;
  }

  /* Installation Tabs */
  .os-tabs {
    display: flex;
    justify-content: center;
    margin-bottom: 40px;
    background: var(--bg-secondary);
    padding: 6px;
    border-radius: 12px;
    width: fit-content;
    margin-left: auto;
    margin-right: auto;
    border: 1px solid var(--border-color);
  }

  .os-tab {
    padding: 8px 24px;
    font-size: 15px;
    font-weight: 600;
    background: transparent;
    border: none;
    color: var(--text-secondary);
    cursor: pointer;
    border-radius: 8px;
    transition: all 0.2s ease;
  }

  .os-tab:hover {
    color: var(--text-color);
  }

  .os-tab.active {
    background: var(--bg-color);
    color: var(--text-color);
    box-shadow: 0 2px 8px rgba(0,0,0,0.1);
  }

  @media (prefers-color-scheme: dark) {
    .os-tab.active {
      box-shadow: 0 2px 8px rgba(0,0,0,0.4);
    }
  }

  .install-content {
    max-width: 700px;
    margin: 0 auto;
  }

  .code-block {
    background: var(--card-bg);
    padding: 30px;
    border-radius: var(--border-radius);
    border: 1px solid var(--border-color);
  }

  .code-block h4 {
    margin-top: 0;
    margin-bottom: 10px;
    font-size: 16px;
  }

  .code-block h4:not(:first-child) {
    margin-top: 24px;
  }

  pre {
    background: var(--bg-color);
    padding: 16px;
    border-radius: 8px;
    overflow-x: auto;
    border: 1px solid var(--border-color);
  }

  code {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
    font-size: 14px;
    color: var(--text-color);
  }

  footer {
    padding: 40px 0;
    text-align: center;
    border-top: 1px solid var(--border-color);
    color: var(--text-secondary);
  }

  @media (max-width: 768px) {
    .title { font-size: 40px; }
    .subtitle { font-size: 20px; }
    .hero { padding: 80px 0 60px; }
    .features, .install { padding: 60px 0; }
  }
</style>