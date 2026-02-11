import Header from "../components/Header.jsx";
import Footer from "../components/Footer.jsx";

export default function MainLayout({ children, view, onNavigate }) {
  return (
    <div class="page">
      <Header view={view} onNavigate={onNavigate} />
      <main class="content">{children}</main>
      <Footer />
    </div>
  );
}
