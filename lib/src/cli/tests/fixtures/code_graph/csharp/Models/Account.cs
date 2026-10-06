namespace Bank.Models
{
    public interface IAccount
    {
        decimal Balance();
    }

    public class Account : IAccount
    {
        private decimal total;

        public void Deposit(decimal amount)
        {
            total += Validate(amount);
        }

        public decimal Balance()
        {
            return total;
        }

        private static decimal Validate(decimal amount)
        {
            return System.Math.Max(amount, 0);
        }
    }
}
